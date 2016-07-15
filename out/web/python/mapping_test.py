import httplib

categories = [ ("Arhitecture", 1),
    ("C & C++", 2),
    ("Common programming. Algorithms", 3),
    ("Components docs", 4),
    ("Database", 5),
    (".NET", 6),
    ("Game development", 7),
    ("GUI", 8),
    ("Java", 10),
    ("Linux programming", 11) ]
    
cat_by_id = {}
for cat in categories: cat_by_id [ str(cat[1]) ] = cat[0]

# format output
_get = http_context.request.get
id = ""
if "id" in _get: 
    id = _get["id"]

if (http_context.initial_virtual_path.endswith(".py")):
    http_context.process_error ("This script cannot be used directly.")
    exit ()
    
body = ""
body += "Initial virtual path: %s <br/>" % http_context.initial_virtual_path
body += "Mapped virtual path: %s <br/>" % http_context.virtual_path
body += "Mapped url: %s <br/>" % http_context.path

body += "<br/><a href=\"/\">go to root</a><br/>"
    
if id != "":
    parentPath = http_context.initial_virtual_path
    if (parentPath.endswith ('/')): parentPath = parentPath.rstrip ('/')
    parentPath = parentPath[0 : parentPath.rfind ('/') +1]

    if id in cat_by_id:
        sel_cat = cat_by_id[id]
    else:
        sel_cat = "Unknown"
    
    body += "<hr/>Selected: <b>%s</b>" % (sel_cat)
    body += "<br /><br /><a href=\"%s\">Go to categories list</a><br/>" % parentPath
else:
    body += "<br/><h1>Categories</h1>"
    for cat in categories: 
        body += "<a href=\"%d\">%s</a><br/>" % (cat[1], cat[0])   


    

template = """<html><head>
<title>Mappings test</title>
<style> BODY { padding: 10px; margin: 10px; font: 10pt Tahoma, Arial; color: #000;} 
    H1 {font-size: 12pt; font-weight: bold; } 
    HR {height:1px; border: 1px solid #333; color: #333;} 
    TABLE {font-size: 100%;} 
    A:link, A:visited {color: #cc0000;}
</style></head>
<body>
{body}
 <hr />
 </body></html>"""
   
http_context.write ( template.replace("{body}", body) )