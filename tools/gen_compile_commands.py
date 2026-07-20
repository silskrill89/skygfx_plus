"""Generate compile_commands.json from build/skygfx.vcxproj for clangd."""
import xml.etree.ElementTree as ET
import json, os, sys

ns = '{http://schemas.microsoft.com/developer/msbuild/2003}'
root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
vcxproj = os.path.join(root_dir, 'build', 'skygfx.vcxproj')

if not os.path.exists(vcxproj):
    print(f'Error: {vcxproj} not found. Run premake5 first.', file=sys.stderr)
    sys.exit(1)

tree = ET.parse(vcxproj)
root = tree.getroot()

defines = []
includes = []

for ig in root.findall(f'{ns}ItemDefinitionGroup'):
    cond = ig.get('Condition', '')
    if 'Debug' not in cond:
        continue
    cc = ig.find(f'{ns}ClCompile')
    if cc is None:
        continue
    pd = cc.find(f'{ns}PreprocessorDefinitions')
    if pd is not None and pd.text:
        for d in pd.text.split(';'):
            d = d.strip()
            if d and d != '%(PreprocessorDefinitions)':
                defines.append(d)
    ai = cc.find(f'{ns}AdditionalIncludeDirectories')
    if ai is not None and ai.text:
        for i in ai.text.split(';'):
            i = i.strip()
            if i and i != '%(AdditionalIncludeDirectories)':
                if i.startswith('..'):
                    i = os.path.normpath(os.path.join(root_dir, 'build', i))
                includes.append(i)

src_files = []
for cg in root.findall(f'{ns}ItemGroup'):
    for cc in cg.findall(f'{ns}ClCompile'):
        inc = cc.get('Include')
        if inc:
            p = os.path.normpath(os.path.join(root_dir, 'build', inc))
            if os.path.exists(p):
                src_files.append(os.path.abspath(p))

compile_commands = []
for src in src_files:
    cmd = f'clang++ -std=c++17 -target i686-pc-windows-msvc'
    for d in defines:
        cmd += f' -D{d}'
    cmd += ' -D_CRT_USE_MM_LOADU_SI64=0'
    for i in includes:
        cmd += f' -I"{i}"'
    cmd += f' -c "{src}" -o "{src}.o"'
    compile_commands.append({
        'directory': os.path.abspath(root_dir),
        'command': cmd,
        'file': src
    })

out = os.path.join(root_dir, 'compile_commands.json')
with open(out, 'w') as f:
    json.dump(compile_commands, f, indent=2)

print(f'Generated {out} with {len(compile_commands)} entries')
