
include "vec.lobster"
include "color.lobster"

fatal(gl_window("logtest", 1024, 1024))

while(gl_frame()):
    
    if(gl_wentdown("escape")): return
    
    gl_clear(color_black)
    gl_color(color_white)
    
    function a():
        v ?= 0
        v++
        
    function b():
        if(a() % 4):
            v ?= 0
            v++

    b()
    