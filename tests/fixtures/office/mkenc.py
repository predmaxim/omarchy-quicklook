# Usage: python3 mkenc.py <outdir> <port>  -- needs a running headless soffice with --accept on <port>
import sys, time, uno
from com.sun.star.beans import PropertyValue
def pv(n, v):
    p = PropertyValue(); p.Name = n; p.Value = v; return p
out, port = sys.argv[1], sys.argv[2]
ctx = uno.getComponentContext()
res = ctx.ServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", ctx)
for _ in range(60):
    try:
        remote = res.resolve(f"uno:socket,host=localhost,port={port};urp;StarOffice.ComponentContext"); break
    except Exception: time.sleep(0.5)
desktop = remote.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", remote)
def mk(kind, name, flt, fill):
    d = desktop.loadComponentFromURL(f"private:factory/{kind}", "_blank", 0, (pv("Hidden", True),))
    fill(d)
    d.storeToURL(f"file://{out}/{name}", (pv("FilterName", flt), pv("Password", "secret")))
    d.close(True)
txt = lambda d: d.Text.setString("secret document")
cell = lambda d: d.Sheets.getByIndex(0).getCellByPosition(0, 0).setString("secret")
mk("swriter", "enc.docx", "MS Word 2007 XML", txt)
mk("scalc", "enc.xlsx", "Calc MS Excel 2007 XML", cell)
mk("swriter", "enc.odt", "writer8", txt)
try: desktop.terminate()
except Exception: pass
