import re, glob, os

files = sorted(glob.glob('shaders/ps/*.hlsl') + glob.glob('shaders/vs/*.hlsl'))
for f in files:
    regs = []
    with open(f, encoding='utf-8', errors='ignore') as fh:
        for line in fh:
            for m in re.finditer(r'register\((s|c)(\d+)\)', line):
                regs.append((m.group(1), int(m.group(2))))
    if regs:
        tokens = ', '.join(f'{t}{n}' for t,n in regs)
        print(f'[{os.path.basename(f)}] ' + tokens)