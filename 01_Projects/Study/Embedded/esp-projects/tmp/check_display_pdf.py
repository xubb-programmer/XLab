import pathlib
import pdfplumber

doc_dir = pathlib.Path(r'D:\XLab\XProjects\esp-projects\doc')

for name in ['DNESP32S3开发板使用前必看说明(IDF版)_V1.0.pdf',
             'DNESP32S3 V1.0 硬件参考手册.pdf']:
    pdf = doc_dir / name
    if not pdf.exists():
        print('missing:', name)
        continue
    print('\n=====', name, '=====')
    with pdfplumber.open(pdf) as doc:
        for i, page in enumerate(doc.pages):
            text = page.extract_text() or ''
            for kw in ['OLED', 'LCD', '屏幕', '显示屏', '背光']:
                if kw in text:
                    lines = [l.strip() for l in text.splitlines() if kw in l]
                    print(f'p{i+1} [{kw}]:', ' | '.join(lines[:4]))
                    break
