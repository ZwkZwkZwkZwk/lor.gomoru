from pathlib import Path
text=''.join(p.read_text() for p in Path('src').glob('*') if p.suffix in ('.cpp','.hpp'))
chars=set(text)|set(chr(c) for c in range(32,127))
Path('assets/glyphs.txt').write_text(''.join(sorted(c for c in chars if ord(c)>=32)))
