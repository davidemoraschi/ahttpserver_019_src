# -*- coding: utf-8 -*-

import time
http_context.set_utf8_html_response()

body = ""

def safe (s):
    if s == None:
        return ""
    return s

template = """<html><head>
<title>POST test</title>
<style> 
    BODY { padding: 10px; margin: 10px; } 
    BODY, INPUT, SELECT, TEXTAREA {font: 10pt Tahoma, Arial; color: #000;}
    H1 {font-size: 12pt; font-weight: bold; } 
    HR {height:1px; border: 1px solid #333; color: #333;} 
    TABLE {font-size: 100%%;} 
</style></head>
<body onload="document.getElementById('Submit').focus(); ">
<h1>POST request test</h1>
 <hr />
 <form method="post">
    <input type="hidden" name="ts" value="%d">
    <input type="text" name="f1" value="%s">
    <input type="text" name="f2" value="%s">
    <input type="text" name="колба" value="%s">
    <br />
    <textarea name="text" rows="20">%s</textarea>
    <br />
    <input type="submit" id="Submit">
 </form>
 </body></html>""" % (  time.time(),
    safe (http_context.request["f1"]), 
    safe (http_context.request["f2"]), 
    safe (http_context.request["колба"]), 
    safe (http_context.request["text"]), )
   
http_context.write ( template )
