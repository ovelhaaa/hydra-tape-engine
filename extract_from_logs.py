import json
import os

log_path = r"C:\Users\devx\.gemini\antigravity\brain\fd9b0664-8b50-4cc1-b885-ddcfd8cd72f3\.system_generated\logs\overview.txt"
output_dir = r"c:\Users\devx\Documents\PlatformIO\Projects\hydra\extracted_logs"

if not os.path.exists(output_dir):
    os.makedirs(output_dir)

with open(log_path, 'r', encoding='utf-8') as f:
    for i, line in enumerate(f):
        try:
            entry = json.loads(line.strip())
            if 'tool_calls' in entry:
                for tool in entry['tool_calls']:
                    if tool['name'] == 'write_to_file':
                        args = tool['args']
                        # Sometimes args is a string containing JSON
                        if isinstance(args, str):
                            try:
                                args = json.loads(args)
                            except:
                                pass
                        
                        # Strip quotes from arguments if present
                        target = args.get('TargetFile', '').strip('"')
                        content = args.get('CodeContent', '')
                        if content.startswith('"') and content.endswith('"'):
                            # Unescape
                            try:
                                content = json.loads(content)
                            except:
                                content = content[1:-1].replace('\\n', '\n').replace('\\"', '"')
                        
                        if target:
                            filename = os.path.basename(target)
                            out_path = os.path.join(output_dir, f"step_{entry.get('step_index')}_{filename}")
                            with open(out_path, 'w', encoding='utf-8') as out_f:
                                out_f.write(content)
                            print(f"Extracted {filename} from step {entry.get('step_index')} to {out_path}")
        except Exception as e:
            print(f"Error parsing line {i}: {e}")
