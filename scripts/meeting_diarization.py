#!/usr/bin/env python3
"""Transcribe both channels and identify remote speakers locally."""
import argparse
import array
import contextlib
import collections
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import wave
import warnings
from pathlib import Path

LINE = re.compile(r'^\[(\d+):(\d+):(\d+(?:\.\d+)?)\s*-->\s*(\d+):(\d+):(\d+(?:\.\d+)?)\]\s*(.*)$')
NON_SPEECH = re.compile(r'^\s*[\[(](?:blank[_ ]audio|silence|music|noise|applause)[\])]\s*$', re.I)
child = None
WHISPER_PROGRESS = re.compile(r'progress\s*=\s*(\d{1,3})\s*%')


def progress(stage, percent=None):
    event = {'stage': stage}
    if percent is not None:
        event['percent'] = percent
    print('STC_PROGRESS ' + json.dumps(event), file=sys.stderr, flush=True)


def stop_child(_signum, _frame):
    if child is not None:
        child.terminate()
        child.wait()
    raise SystemExit(143)


def run(args, env=None, on_progress=None):
    global child
    child = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    errors = collections.deque(maxlen=200)

    def read_errors():
        for line in child.stderr:
            errors.append(line[-4000:])
            match = WHISPER_PROGRESS.search(line)
            if on_progress and match and 0 <= int(match.group(1)) <= 100:
                on_progress(int(match.group(1)))

    reader = threading.Thread(target=read_errors, daemon=True)
    reader.start()
    out = child.stdout.read()
    code = child.wait()
    reader.join()
    child = None
    if code:
        raise RuntimeError(f"{Path(args[0]).name} failed: {''.join(errors).strip()[-4000:]}")
    return out


def parse_whisper(output):
    segments = []
    matched = False
    for line in output.splitlines():
        match = LINE.match(line.strip())
        if not match:
            continue
        matched = True
        h1, m1, s1, h2, m2, s2, text = match.groups()
        if text.strip() and not NON_SPEECH.match(text):
            segments.append({'start': int(h1)*3600 + int(m1)*60 + float(s1),
                             'end': int(h2)*3600 + int(m2)*60 + float(s2), 'text': text.strip()})
    if output.strip() and not matched:
        raise RuntimeError('Could not parse whisper-cli timestamps.')
    return segments


def assign_speakers(segments, turns):
    labels = {}
    for turn in sorted(turns, key=lambda x: x[0]):
        if turn[2] not in labels:
            labels[turn[2]] = f'Osoba {len(labels) + 1}'
    for segment in segments:
        overlaps = {}
        for start, end, label in turns:
            overlap = max(0.0, min(segment['end'], end) - max(segment['start'], start))
            overlaps[label] = overlaps.get(label, 0.0) + overlap
        ordered = sorted(overlaps.items(), key=lambda x: x[1], reverse=True)
        total = sum(overlaps.values())
        segment['speaker'] = (labels[ordered[0][0]] if total > 0 and ordered[0][1] / total >= 0.7
                              else 'Osoba nieustalona')
    return segments


def load_pcm_wav(path, torch):
    """Read the mono 16-bit WAV produced by FFmpeg without torchcodec."""
    with wave.open(str(path), 'rb') as audio:
        if audio.getnchannels() != 1 or audio.getsampwidth() != 2 or audio.getcomptype() != 'NONE':
            raise ValueError('Expected uncompressed mono 16-bit PCM audio.')
        sample_rate = audio.getframerate()
        samples = array.array('h')
        samples.frombytes(audio.readframes(audio.getnframes()))
    if sys.byteorder != 'little':
        samples.byteswap()
    if not samples:
        raise ValueError('The output channel contains no audio samples.')
    waveform = torch.frombuffer(samples, dtype=torch.int16).to(dtype=torch.float32).div_(32768.0).unsqueeze(0)
    return {'waveform': waveform, 'sample_rate': sample_rate}


def main():
    parser = argparse.ArgumentParser()
    for option in ('audio', 'whisper', 'model', 'language', 'threads'):
        parser.add_argument('--' + option, required=True)
    args = parser.parse_args()
    progress('loading_model')
    # This sidecar supplies decoded PCM; pyannote's optional file decoder is unused.
    warnings.filterwarnings('ignore', message=r'(?s).*torchcodec is not installed correctly.*',
                            category=UserWarning, module=r'pyannote\.audio\.core\.io')
    from pyannote.audio import Pipeline
    import torch

    token = os.environ.get('HF_TOKEN') or None
    pipeline = Pipeline.from_pretrained('pyannote/speaker-diarization-community-1', token=token)
    if pipeline is None:
        raise RuntimeError('Cannot load pyannote model. Accept its Hugging Face conditions and set HF_TOKEN.')
    pipeline.to(torch.device('cpu'))
    cli = Path(args.whisper).expanduser().resolve()
    build = cli.parent.parent
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = ':'.join([str(build / 'src'), str(build / 'ggml/src'), env.get('LD_LIBRARY_PATH', '')])
    with tempfile.TemporaryDirectory(prefix='speak-to-computer-meeting-') as temp:
        transcripts = []
        for channel in range(2):
            prepare_stage = 'prepare_microphone' if channel == 0 else 'prepare_remote'
            whisper_stage = 'transcribe_microphone' if channel == 0 else 'transcribe_remote'
            progress(prepare_stage)
            wav = str(Path(temp) / f'channel-{channel}.wav')
            run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', args.audio, '-af',
                 f'pan=mono|c0=c{channel}', '-ar', '16000', '-c:a', 'pcm_s16le', '-y', wav])
            progress(whisper_stage)
            output = run([str(cli), '-m', args.model, '-f', wav, '-l', args.language,
                          '-t', args.threads, '-np', '-pp'], env,
                         on_progress=lambda percent: progress(whisper_stage, percent))
            segments = parse_whisper(output)
            if channel == 0:
                for segment in segments:
                    segment['speaker'] = 'Ja'
            elif segments:
                progress('speaker_segmentation')

                def hook(step_name, step_artifact, file=None, total=None, completed=None):
                    if step_name in ('segmentation', 'embeddings'):
                        if total is not None and completed is not None and total > 0:
                            progress('speaker_' + step_name, min(100, max(0, int(100 * completed / total))))
                    elif step_name == 'speaker_counting':
                        progress('speaker_counting')
                    elif step_name == 'discrete_diarization':
                        progress('speaker_finalize')

                diarization = pipeline(load_pcm_wav(wav, torch), hook=hook).exclusive_speaker_diarization
                turns = [(segment.start, segment.end, speaker) for segment, speaker in diarization]
                assign_speakers(segments, turns)
            transcripts.extend(segments)
        if not transcripts:
            raise RuntimeError('Whisper returned no text on either channel.')
        progress('saving')
        return {'segments': sorted(transcripts, key=lambda x: (x['start'], x['end']))}


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, stop_child)
    try:
        with contextlib.redirect_stdout(sys.stderr):
            result = main()
        print(json.dumps(result, ensure_ascii=False))
    except Exception as exc:
        print(f'Diarization failed: {exc}', file=sys.stderr)
        sys.exit(1)
