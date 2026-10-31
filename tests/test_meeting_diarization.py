import importlib.util
import io
import pathlib
import struct
import sys
import tempfile
import types
import unittest
import wave
from unittest import mock

path = pathlib.Path(__file__).resolve().parents[1] / 'scripts' / 'meeting_diarization.py'
spec = importlib.util.spec_from_file_location('meeting_diarization', path)
diarization = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diarization)


class MeetingDiarizationTest(unittest.TestCase):
    def test_pcm_wav_converts_samples_to_mono_float_waveform(self):
        try:
            import torch
        except ImportError:
            self.skipTest('PyTorch is not installed in the test interpreter')
        with tempfile.TemporaryDirectory() as temp:
            wav = pathlib.Path(temp) / 'audio.wav'
            with wave.open(str(wav), 'wb') as audio:
                audio.setnchannels(1)
                audio.setsampwidth(2)
                audio.setframerate(16000)
                audio.writeframes(struct.pack('<hhh', -32768, 0, 16384))
            result = diarization.load_pcm_wav(wav, torch)
            self.assertEqual(result['sample_rate'], 16000)
            self.assertEqual(result['waveform'].shape, (1, 3))
            self.assertEqual(result['waveform'].tolist(), [[-1.0, 0.0, 0.5]])
            with wave.open(str(wav), 'wb') as audio:
                audio.setnchannels(2)
                audio.setsampwidth(2)
                audio.setframerate(16000)
                audio.writeframes(struct.pack('<hh', 0, 0))
            with self.assertRaisesRegex(ValueError, 'mono'):
                diarization.load_pcm_wav(wav, torch)

    def test_parse_whisper_preserves_timing_and_polish(self):
        segments = diarization.parse_whisper('[01:02:03.250 --> 01:02:04.750] Cześć!\n')
        self.assertEqual(segments, [{'start': 3723.25, 'end': 3724.75, 'text': 'Cześć!'}])
        self.assertEqual(diarization.parse_whisper('[00:00:00.000 --> 00:00:01.000] [BLANK_AUDIO]'), [])
        with self.assertRaises(RuntimeError):
            diarization.parse_whisper('Unexpected output')

    def test_assign_speakers_by_time_and_leave_ambiguous_unknown(self):
        turns = [(4, 6, 'B'), (0, 2, 'A'), (6, 8, 'A')]
        segments = [{'start': 0, 'end': 2}, {'start': 4, 'end': 6},
                    {'start': 5, 'end': 7}, {'start': 9, 'end': 10}, {'start': 6, 'end': 8}]
        diarization.assign_speakers(segments, turns)
        self.assertEqual([s['speaker'] for s in segments],
                         ['Osoba 1', 'Osoba 2', 'Osoba nieustalona', 'Osoba nieustalona', 'Osoba 1'])

    def test_run_streams_whisper_progress_and_preserves_stdout(self):
        received = []
        code = "import sys; print('progress =  42%', file=sys.stderr, flush=True); print('transcript')"
        output = diarization.run([sys.executable, '-c', code], on_progress=received.append)
        self.assertEqual(received, [42])
        self.assertEqual(output, 'transcript\n')

    def test_pipeline_transcribes_channels_and_labels_remote_voice(self):
        class Pipeline:
            @classmethod
            def from_pretrained(cls, *args, **kwargs):
                return cls()

            def to(self, device):
                pass

            def __call__(self, waveform, hook=None):
                assert waveform == {'waveform': 'samples', 'sample_rate': 16000}
                hook('segmentation', None, total=4, completed=2)
                hook('embeddings', None, total=4, completed=3)
                return types.SimpleNamespace(exclusive_speaker_diarization=[
                    (types.SimpleNamespace(start=1, end=2), 'SPEAKER_00')])

        pyannote = types.ModuleType('pyannote')
        audio = types.ModuleType('pyannote.audio')
        audio.Pipeline = Pipeline
        torch = types.ModuleType('torch')
        torch.device = lambda value: value
        calls = []

        def run(args, env=None, on_progress=None):
            calls.append(args)
            if args[0] == 'ffmpeg':
                return ''
            if on_progress:
                on_progress(42)
            if args[args.index('-f') + 1].endswith('channel-0.wav'):
                return '[00:00:00.000 --> 00:00:01.000] Ja mówię\n'
            return '[00:00:01.000 --> 00:00:02.000] Rozmówca mówi\n'

        argv = ['meeting_diarization.py', '--audio', 'input.flac', '--whisper', '/whisper/build/bin/whisper-cli',
                '--model', 'model.bin', '--language', 'pl', '--threads', '4']
        errors = io.StringIO()
        with mock.patch.dict(sys.modules, {'pyannote': pyannote, 'pyannote.audio': audio, 'torch': torch}), \
                mock.patch.object(sys, 'argv', argv), mock.patch.object(diarization, 'run', side_effect=run), \
                mock.patch.object(diarization, 'load_pcm_wav', return_value={'waveform': 'samples', 'sample_rate': 16000}), \
                mock.patch.object(sys, 'stderr', errors):
            result = diarization.main()
        self.assertEqual([s['speaker'] for s in result['segments']], ['Ja', 'Osoba 1'])
        self.assertEqual(sum(1 for c in calls if c[0] == 'ffmpeg'), 2)
        self.assertTrue(any('pan=mono|c0=c0' in c for c in calls))
        self.assertTrue(any('pan=mono|c0=c1' in c for c in calls))
        self.assertTrue(all('-pp' in c for c in calls if c[0].endswith('whisper-cli')))
        self.assertIn('"stage": "transcribe_microphone", "percent": 42', errors.getvalue())
        self.assertIn('"stage": "speaker_segmentation", "percent": 50', errors.getvalue())
        self.assertIn('"stage": "speaker_embeddings", "percent": 75', errors.getvalue())


if __name__ == '__main__':
    unittest.main()
