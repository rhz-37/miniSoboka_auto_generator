import re

levels_text = open(r'F:\OpenGL\BOX\src\levels.h', encoding='utf-8').read()

# Find all level blocks
pattern = r'\{ // (Easy|Medium|Hard|Hell) #(\d+)'
matches = list(re.finditer(pattern, levels_text))

for i, m in enumerate(matches):
    diff = m.group(1)
    num = m.group(2)
    start = m.start()
    end_pos = matches[i+1].start() if i+1 < len(matches) else len(levels_text)
    block = levels_text[start:end_pos]
    
    lines = re.findall(r'"([^"]*)"', block)
    
    goals = sum(1 for l in lines for ch in l if ch in '.+*')
    boxes = sum(1 for l in lines for ch in l if ch in '$*')
    
    if goals != boxes:
        print(f'=== {diff} #{num}: goals={goals}, boxes={boxes} ===')
        for line in lines:
            print(f'  "{line}"')
        print()
