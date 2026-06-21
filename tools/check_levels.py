import re

levels_text = open(r'F:\OpenGL\BOX\src\levels.h', encoding='utf-8').read()

# Find all level blocks
pattern = r'\{ // (Easy|Medium|Hard|Hell) #(\d+)'
matches = list(re.finditer(pattern, levels_text))

for i, m in enumerate(matches):
    diff = m.group(1)
    num = m.group(2)
    start = m.start()
    # Find the closing brace of the block
    end_pos = matches[i+1].start() if i+1 < len(matches) else len(levels_text)
    block = levels_text[start:end_pos]
    
    # Extract all string literals
    lines = re.findall(r'"([^"]*)"', block)
    
    goals = 0
    boxes = 0
    for line in lines:
        for ch in line:
            if ch in '.+*':
                goals += 1
            if ch in '$*':
                boxes += 1
    
    status = 'OK' if goals == boxes else 'MISMATCH'
    print(f'{diff} #{num}: goals={goals}, boxes={boxes} [{status}]')
