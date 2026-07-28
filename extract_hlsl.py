import PyPDF2
import os

pdf_path = r'E:\dev(dave)\GTA SA Reverse Engineering Documentation\SDK Docs\Introduction to DX9 HLSL - Mitchell Peeper.pdf'
out_path = r'E:\dev(dave)\skygfx_plus_expIV\hlsl_intro_extracted.txt'

reader = PyPDF2.PdfReader(pdf_path)
total_pages = len(reader.pages)
print(f"Total pages: {total_pages}")

all_text = []
for i, page in enumerate(reader.pages):
    text = page.extract_text()
    all_text.append(f"--- PAGE {i+1} ---\n{text}\n")

full_text = "\n".join(all_text)
with open(out_path, "w", encoding="utf-8") as f:
    f.write(full_text)

print(f"Extracted {len(full_text)} chars to {out_path}")
