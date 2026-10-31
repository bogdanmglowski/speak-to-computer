import importlib.util
import http.server
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import unittest
from unittest import mock

path = pathlib.Path(__file__).resolve().parents[1] / 'scripts' / 'meeting_analysis.py'
spec = importlib.util.spec_from_file_location('meeting_analysis', path)
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def note(text='Wniosek', sources=None):
    result = {key: [] for key, _ in analysis.SECTIONS}
    result['topics'] = [{'text': text, 'sources': sources or []}]
    return result


class MeetingAnalysisTest(unittest.TestCase):
    def test_cli_with_real_local_http_and_stdin(self):
        calls = []

        class Server(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def send_json(self, value):
                content = json.dumps(value).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(content)))
                self.end_headers()
                self.wfile.write(content)

            def do_GET(self):
                self.send_json({'models': [{'name': 'test:latest'}, {'name': 'remote:cloud', 'remote_host': 'example.com'}]})

            def do_POST(self):
                payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                calls.append(payload)
                self.send_json({'done': True, 'done_reason': 'stop', 'message': {
                    'content': json.dumps(note('Ustalono test', ['S1']))}})

        try:
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Server)
        except PermissionError:
            self.skipTest('Local sockets are not permitted in this environment')
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            listed = subprocess.run([sys.executable, str(path), '--list-models', '--backend', 'ollama',
                '--endpoint', f'http://127.0.0.1:{server.server_port}'], text=True, capture_output=True, timeout=10)
            result = subprocess.run([sys.executable, str(path), '--transcript', '-', '--backend', 'ollama',
                '--model', 'test:latest', '--endpoint', f'http://127.0.0.1:{server.server_port}'],
                input='[00:01:02] Ja: Ustalono test.\n', text=True, capture_output=True, timeout=10)
        finally:
            server.shutdown(); server.server_close(); thread.join()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(listed.returncode, 0, listed.stderr)
        self.assertEqual(json.loads(listed.stdout)['models'], ['test:latest'])
        self.assertIn('(00:01:02)', json.loads(result.stdout)['markdown'])
        self.assertIn('ANALYSIS_PROGRESS', result.stderr)
        self.assertEqual(len(calls), 1)
        self.assertIs(calls[0]['truncate'], False)
        self.assertIn('[S1]', calls[0]['messages'][1]['content'])

    def test_split_does_not_lose_unicode_or_long_lines(self):
        text = 'Zażółć gęślą jaźń.\n' * 1200 + 'ą' * 9000
        chunks = analysis.split_text(text, 1000)
        self.assertEqual(''.join(chunks), text)
        self.assertTrue(all(len(chunk.encode('utf-8')) <= 1000 for chunk in chunks))

    def test_model_source_ids_are_checked_against_transcript(self):
        entries, timestamps = analysis.prepare_transcript('[00:01:02.500 --> 00:01:05.000] Ja: Zadanie.\nBez czasu\n')
        self.assertEqual(timestamps, {'S1': '00:01:02', 'S2': ''})
        clean = analysis.normalize(note('Ważne', ['S1', 'S999', 'S1']), {'S1', 'S2'})
        self.assertEqual(clean['topics'][0]['sources'], ['S1'])
        self.assertIn('(00:01:02)', analysis.render_markdown(clean, timestamps))
        clean['topics'][0]['sources'] = ['S2']
        self.assertIn('(brak znacznika źródła)', analysis.render_markdown(clean, timestamps))
        self.assertEqual(len(entries), 2)

    def test_long_meeting_is_processed_in_chunks_and_reduced(self):
        transcript = ''.join(f'[00:00:{i:02d}] Ja: ' + 'zażółć ' * 300 + '\n' for i in range(20))
        calls = []

        def request(backend, model, endpoint, text, reasoning_effort=None, codex_path='codex'):
            self.assertLessEqual(len(text.encode('utf-8')), analysis.LIMIT)
            calls.append(text)
            import re
            match = re.search(r'S\d+', text)
            return note('Ustalenie', [match.group()] if match else [])

        with mock.patch.object(analysis, 'analyze_request', side_effect=request):
            result = analysis.analyze(transcript, 'ollama', 'qwen3:8b', 'http://localhost:11434')
        self.assertGreater(len(calls), 1)
        self.assertIn('Omówione tematy', result)
        self.assertIn('Ustalenie', result)

    def test_large_partial_notes_are_combined_without_extra_requests(self):
        transcript = ''.join(f'[00:00:0{i}] ' + str(i) * 6000 + '\n' for i in range(1, 5))
        calls = []

        def request(backend, model, endpoint, text, reasoning_effort=None, codex_path='codex'):
            import re
            source = re.search(r'S\d+', text).group()
            calls.append(source)
            return note('Punkt ' + source + ' ' + 'x' * 4500, [source])

        with mock.patch.object(analysis, 'analyze_request', side_effect=request):
            result = analysis.analyze(transcript, 'ollama', 'model', 'http://localhost:11434')
        self.assertEqual(calls, ['S1', 'S2', 'S3', 'S4'])
        for index in range(1, 5):
            self.assertIn(f'Punkt S{index}', result)
            self.assertIn(f'(00:00:0{index})', result)

    def test_small_partial_notes_get_one_synthesis_with_original_sources(self):
        transcript = ''.join(f'[00:00:0{i}] ' + str(i) * 6000 + '\n' for i in range(1, 4))
        calls = []

        def request(backend, model, endpoint, text, reasoning_effort=None, codex_path='codex'):
            import re
            calls.append(text)
            if len(calls) <= 3:
                return note('Punkt ' + str(len(calls)), [re.search(r'S\d+', text).group()])
            return note('Wspólny wniosek', ['S1', 'S2', 'S3'])

        with mock.patch.object(analysis, 'analyze_request', side_effect=request):
            result = analysis.analyze(transcript, 'ollama', 'model', 'http://localhost:11434')
        self.assertEqual(len(calls), 4)
        self.assertIn('(00:00:01, 00:00:02, 00:00:03)', result)

    def test_non_shrinking_summaries_are_combined_without_repeated_rounds(self):
        transcript = ''.join(f'[00:00:0{i}] ' + str(i) * 6000 + '\n' for i in range(1, 4))
        calls = []

        def request(backend, model, endpoint, text, reasoning_effort=None, codex_path='codex'):
            import re
            source = re.search(r'S\d+', text).group()
            calls.append(source)
            return note('Ważne ' + source + ' ' + 'x' * 5000, [source])

        with mock.patch.object(analysis, 'analyze_request', side_effect=request):
            result = analysis.analyze(transcript, 'ollama', 'model', 'http://localhost:11434')
        self.assertEqual(calls, ['S1', 'S2', 'S3'])
        self.assertIn('połączono z analiz fragmentów', result)
        for index in range(1, 4):
            self.assertIn(f'Ważne S{index}', result)
            self.assertIn(f'(00:00:0{index})', result)

    def test_combined_notes_keep_sources_for_identical_points(self):
        combined = analysis.combine_notes([note('To samo', ['S1']), note('To samo', ['S2', 'S1'])])
        self.assertEqual(combined['topics'], [{'text': 'To samo', 'sources': ['S1', 'S2']}])

    def test_reasoning_effort_is_forwarded_to_all_chunks(self):
        efforts = []
        def request(backend, model, endpoint, text, reasoning_effort=None, codex_path='codex'):
            efforts.append(reasoning_effort)
            import re
            match = re.search(r'S\d+', text)
            return note('Wniosek', [match.group()] if match else [])
        transcript = ''.join(f'[00:00:{i:02d}] ' + 'tekst ' * 1000 + '\n' for i in range(8))
        with mock.patch.object(analysis, 'analyze_request', side_effect=request):
            analysis.analyze(transcript, 'codex', 'model', '', 'high')
        self.assertGreater(len(efforts), 1)
        self.assertTrue(all(effort == 'high' for effort in efforts))

    def test_ollama_accepts_network_servers_and_does_not_truncate(self):
        for endpoint in ('http://karaczan:11434', 'http://192.168.1.2:11434/',
                         'https://ollama.example.com', 'http://[::1]:11434'):
            analysis.validate_ollama_endpoint(endpoint)
        for endpoint in ('karaczan:11434', 'ftp://karaczan', 'http://', 'http://user:pass@karaczan',
                         'http://karaczan/path', 'http://karaczan?query=yes', 'http://karaczan:99999'):
            with self.assertRaises(ValueError, msg=endpoint):
                analysis.validate_ollama_endpoint(endpoint)
        with self.assertRaises(ValueError):
            analysis.verify_ollama_model('http://127.0.0.1:11434', 'model:cloud')
        with mock.patch.object(analysis, 'list_models', return_value=['qwen3:8b']) as models:
            analysis.verify_ollama_model('http://karaczan:11434', 'qwen3:8b')
        models.assert_called_once_with('ollama', 'http://karaczan:11434')
        response = {'done': True, 'done_reason': 'stop', 'message': {'content': json.dumps(note())}}
        with mock.patch.object(analysis, 'request_json', return_value=response) as request:
            analysis.analyze_request('ollama', 'model', 'http://karaczan:11434', 'input')
        self.assertEqual(request.call_args.args[0], 'http://karaczan:11434/api/chat')
        payload = request.call_args.args[1]
        self.assertIs(payload['truncate'], False)
        self.assertIs(payload['shift'], False)
        with mock.patch.object(analysis, 'request_json', return_value={'done': True, 'done_reason': 'length'}):
            with self.assertRaisesRegex(ValueError, 'niepełną'):
                analysis.analyze_request('ollama', 'model', 'http://localhost:11434', 'input')

    def test_fetch_remote_ollama_models(self):
        payload = {'models': [{'name': 'qwen3:8b'}, {'name': 'remote:cloud', 'remote_host': 'cloud.example'}]}
        with mock.patch.object(analysis.OPENER, 'open', return_value=io.BytesIO(json.dumps(payload).encode())) as opened:
            self.assertEqual(analysis.list_models('ollama', 'http://karaczan:11434'), ['qwen3:8b'])
        self.assertEqual(opened.call_args.args[0].full_url, 'http://karaczan:11434/api/tags')

    def test_local_backend_ignores_configured_remote_endpoint(self):
        with mock.patch.object(sys, 'argv', ['meeting_analysis.py', '--list-models', '--backend', 'ollama_local',
                                           '--endpoint', 'http://karaczan:11434']):
            with mock.patch.object(analysis, 'list_models', return_value=['local:latest']) as models:
                with mock.patch('sys.stdout', new_callable=io.StringIO):
                    analysis.main()
        models.assert_called_once_with('ollama', 'http://127.0.0.1:11434')

    def test_opencode_go_cli_uses_stdin_and_validates_output(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / 'opencode'
            executable.write_text('''#!/usr/bin/env python3
import json, pathlib, sys
if '--version' in sys.argv:
    print('opencode v2.2.5'); sys.exit()
if 'models' in sys.argv:
    print('opencode-go/model-b\\nopencode-go/model-a\\nother/model'); sys.exit()
assert '--standalone' in sys.argv and '--format' in sys.argv
assert sys.argv[sys.argv.index('--model') + 1] == 'opencode-go/model-a'
assert '[S1] prywatny tekst' in sys.stdin.read()
assert 'prywatny tekst' not in ' '.join(sys.argv)
config = json.loads(pathlib.Path('opencode.json').read_text())
assert {'action': 'tool.use', 'resource': '*', 'effect': 'deny'} in config['policies']
result = {k: [] for k in ['tldr', 'topics', 'tasks', 'decisions', 'questions']}
result['topics'] = [{'text': 'Test', 'sources': ['S1']}]
print(json.dumps({'type': 'text', 'part': {'text': json.dumps(result)}}))
print(json.dumps({'type': 'step_finish', 'part': {'reason': 'stop'}}))
''', encoding='utf-8')
            executable.chmod(0o700)
            with mock.patch.object(analysis, 'opencode_executable', return_value=str(executable)):
                self.assertEqual(analysis.list_models('opencode', ''), ['opencode-go/model-a', 'opencode-go/model-b'])
                analysis.verify_opencode_version(str(executable))
                result = analysis.opencode_request(str(executable), 'opencode-go/model-a', '[S1] prywatny tekst')
            self.assertEqual(result['topics'][0]['text'], 'Test')
            with self.assertRaisesRegex(ValueError, 'OpenCode Go'):
                analysis.opencode_request(str(executable), 'other/model', 'test')
            executable.write_text('#!/bin/sh\nprintf \'{"type":"step_finish","part":{"reason":"length"}}\\n\'\n', encoding='utf-8')
            with self.assertRaisesRegex(RuntimeError, 'nie ukończył'):
                analysis.opencode_request(str(executable), 'opencode-go/model-a', 'test')

    def test_http_failure_and_invalid_output_do_not_create_notes(self):
        error = urllib.error.HTTPError('http://localhost', 401, 'unauthorized', {}, None)
        with mock.patch.object(analysis.OPENER, 'open', side_effect=error):
            with self.assertRaisesRegex(RuntimeError, 'HTTP 401'):
                analysis.request_json('http://localhost', {}, {})
        with self.assertRaisesRegex(ValueError, 'nieprawidłową'):
            analysis.normalize({'tldr': []}, {'S1'})

    def test_codex_subprocess_uses_stdin_schema_and_chatgpt_login(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / 'codex'
            trace = pathlib.Path(temp) / 'trace.json'
            fake = '''#!/usr/bin/env python3
import json, os, pathlib, sys
assert not os.environ.get('OPENAI_API_KEY') and not os.environ.get('CODEX_API_KEY')
if sys.argv[1:] == ['login', 'status']:
    print('Logged in using ChatGPT'); sys.exit(0)
args = sys.argv[1:]
assert '--sandbox' in args and args[args.index('--sandbox') + 1] == 'read-only'
assert '--ignore-user-config' in args and '--ephemeral' in args
assert args[args.index('--model') + 1] == 'gpt-custom'
assert args[args.index('--config') + 1] == 'model_reasoning_effort="xhigh"'
schema = json.loads(pathlib.Path(args[args.index('--output-schema') + 1]).read_text())
assert 'topics' in schema['properties']
assert '[S1] prywatny tekst' in sys.stdin.read()
pathlib.Path(os.environ['TEST_CODEX_TRACE']).write_text(json.dumps(args))
result = {k: [] for k in ['tldr', 'topics', 'tasks', 'decisions', 'questions']}
result['topics'] = [{'text': 'Test', 'sources': ['S1']}]
print(json.dumps({'type':'item.completed','item':{'type':'agent_message','text':json.dumps(result)}}))
print(json.dumps({'type':'turn.completed'}))
'''
            executable.write_text(fake, encoding='utf-8'); executable.chmod(0o700)
            with mock.patch.dict(os.environ, {'OPENAI_API_KEY': 'secret-one', 'CODEX_API_KEY': 'secret-two', 'TEST_CODEX_TRACE': str(trace)}):
                path_to_cli = analysis.codex_executable(str(executable))
                analysis.verify_codex_login(path_to_cli)
                result = analysis.codex_request(path_to_cli, 'gpt-custom', '[S1] prywatny tekst', 'xhigh')
            self.assertEqual(result['topics'][0]['text'], 'Test')
            self.assertNotIn('prywatny tekst', trace.read_text())
            self.assertNotIn('secret-one', trace.read_text())

    def test_codex_requires_chatgpt_login_and_rejects_incomplete_result(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / 'codex'
            executable.write_text('#!/bin/sh\necho "Logged in using an API key"\n', encoding='utf-8')
            executable.chmod(0o700)
            with self.assertRaisesRegex(ValueError, 'ChatGPT'):
                analysis.verify_codex_login(str(executable))
            executable.write_text('#!/bin/sh\nprintf \'{"type":"turn.failed"}\\n\'\nexit 1\n', encoding='utf-8')
            with self.assertRaisesRegex(RuntimeError, 'nie ukończył'):
                analysis.codex_request(str(executable), '', 'test')
            executable.write_text('#!/bin/sh\necho rate_limit_exceeded >&2\nexit 1\n', encoding='utf-8')
            with self.assertRaisesRegex(RuntimeError, 'limit użycia'):
                analysis.codex_request(str(executable), '', 'test')

    def test_codex_model_cache_is_optional(self):
        with tempfile.TemporaryDirectory() as temp:
            with mock.patch.dict(os.environ, {'CODEX_HOME': temp}):
                self.assertEqual(analysis.list_models('codex', ''), [])
                pathlib.Path(temp, 'models_cache.json').write_text(json.dumps({'models': [
                    {'slug': 'gpt-b', 'visibility': 'list'}, {'slug': 'hidden', 'visibility': 'hide'},
                    {'slug': 'gpt-a', 'visibility': 'list'}]}))
                self.assertEqual(analysis.list_models('codex', ''), ['gpt-a', 'gpt-b'])

    def test_codex_cli_mode_processes_transcript_without_api_key(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / 'codex'
            executable.write_text('''#!/usr/bin/env python3
import json, os, sys
assert not os.environ.get('OPENAI_API_KEY') and not os.environ.get('CODEX_API_KEY')
if sys.argv[1:] == ['login', 'status']:
    print('Logged in using ChatGPT'); sys.exit(0)
assert '--model' not in sys.argv
assert '[S1]' in sys.stdin.read()
result = {k: [] for k in ['tldr', 'topics', 'tasks', 'decisions', 'questions']}
result['topics'] = [{'text': 'Wniosek testowy', 'sources': ['S1']}]
print(json.dumps({'type': 'item.completed', 'item': {'type': 'agent_message', 'text': json.dumps(result)}}))
print(json.dumps({'type': 'turn.completed'}))
''', encoding='utf-8')
            executable.chmod(0o700)
            env = os.environ.copy(); env.update(OPENAI_API_KEY='not-used', CODEX_API_KEY='not-used')
            result = subprocess.run([sys.executable, str(path), '--transcript', '-', '--backend', 'codex',
                                     '--codex-cli', str(executable)], input='[00:01:02] Test.\n',
                                    text=True, capture_output=True, timeout=10, env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Wniosek testowy (00:01:02)', json.loads(result.stdout)['markdown'])

    @unittest.skipUnless(os.name == 'posix', 'Process group handling is POSIX-specific')
    def test_cancel_stops_codex_child(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / 'codex'
            marker = pathlib.Path(temp) / 'child-pid'
            executable.write_text('''#!/usr/bin/env python3
import os, pathlib, sys, time
if sys.argv[1:] == ['login', 'status']:
    print('Logged in using ChatGPT'); sys.exit(0)
sys.stdin.read()
pathlib.Path(os.environ['TEST_CODEX_PID']).write_text(str(os.getpid()))
time.sleep(30)
''', encoding='utf-8')
            executable.chmod(0o700)
            env = os.environ.copy(); env['TEST_CODEX_PID'] = str(marker)
            process = subprocess.Popen([sys.executable, str(path), '--transcript', '-', '--backend', 'codex',
                                        '--codex-cli', str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, env=env)
            try:
                process.stdin.write('[00:01:02] Test.\n'); process.stdin.close(); process.stdin = None
                deadline = time.monotonic() + 10
                while not marker.exists() and time.monotonic() < deadline and process.poll() is None:
                    time.sleep(0.02)
                self.assertTrue(marker.exists(), 'Codex child did not start')
                pid = int(marker.read_text())
                process.terminate(); process.communicate(timeout=5)
                with self.assertRaises(ProcessLookupError):
                    os.kill(pid, 0)
            finally:
                if process.poll() is None:
                    process.kill(); process.communicate(timeout=5)


if __name__ == '__main__':
    unittest.main()
