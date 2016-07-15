import os
# from python_handler import *

def mapPath (context, virtPath):
    context.write ( "<b>Map path (\"%s\"): </b>%s<br/>" \
        % (virtPath, context.map_path(virtPath)) )

mapPath (http_context, "")
mapPath (http_context, ".")
mapPath (http_context, "/")
mapPath (http_context, "../")
mapPath (http_context, "../../python/get.py")
mapPath (http_context, "../test")

try:
    mapPath (http_context, "../../../")
except Exception,ex:
     http_context.write ( "<span style='color: #cc0000; font-weight: bold; '>Error occured: </span>%s<br/>" \
        % (str (ex)))
