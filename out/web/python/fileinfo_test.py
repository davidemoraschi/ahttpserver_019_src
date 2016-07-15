import sys, os
http_context.set_utf8_html_response()


folder = os.path.dirname(http_context.script_path)
if folder not in sys.path: 
    sys.path.append (folder)

import fileinfo 

#FOLDER = "/media/sda5/music/electronic"
FOLDER = "d:\\music\\electronic"

for info in fileinfo.listDirectory(FOLDER, [".mp3"]): 
    http_context.write ("<br/>".join( ["%s=%s" % (k, v) for k, v in info.items()] ))
    http_context.write ("<hr/>")
