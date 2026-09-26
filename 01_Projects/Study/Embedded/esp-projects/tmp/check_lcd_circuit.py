import pdfplumber

pdf = r'D:\XLab\XProjects\esp-projects\tmp\schematic.pdf'
with pdfplumber.open(pdf) as doc:
    print('total pages:', len(doc.pages))
    for i, page in enumerate(doc.pages):
        text = page.extract_text() or ''
        for kw in ['SLCD', 'SPILCD', 'LCD_BL', '背光', 'PWR', 'RST']:
            if kw in text:
                lines = [l.strip() for l in text.splitlines() if kw in l]
                print(f'--- p{i+1} [{kw}] ---')
                print(' | '.join(lines[:5]))
                break
