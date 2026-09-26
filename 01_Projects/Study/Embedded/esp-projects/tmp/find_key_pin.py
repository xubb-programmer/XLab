import pathlib
import pdfplumber

doc_dir = pathlib.Path(r'D:\XLab\XProjects\esp-projects\doc')
pdf = next(doc_dir.glob('DNESP32S3 V1.0 *.pdf'))

with pdfplumber.open(pdf) as doc:
    for i, page in enumerate(doc.pages[:30]):
        text = page.extract_text() or ''
        for kw in ['BOOT', '按键', 'KEY', 'IO0']:
            if kw in text:
                lines = [l.strip() for l in text.splitlines() if kw in l]
                print(f'p{i+1} [{kw}]:', ' | '.join(lines[:3]))
                break
