-- SPDX-License-Identifier: GPL-3.0-or-later
-- Generated config contains only paths and authored button states.
local cfg=dofile([[CONFIG_PATH]])
local function put(name,bytes)
    local f=assert(io.open(cfg.out..name,"wb")); assert(f:write(bytes)); assert(f:close())
end
local function bytes(domain,size)
    local t={}; for i=0,size-1 do t[#t+1]=string.char(emu.read(i,emu.memType[domain])) end
    return table.concat(t)
end
emu.setState({["vdp.scanline"]=0,["vdp.vcounter"]=0})
local expected=assert(io.open(cfg.cart,"rb"));local image=expected:read("*a");expected:close()
assert(bytes("smsCartRam",8192)==image,"Initial cartridge RAM mismatch before first instruction")
local held=cfg.inputs[1]
local function input()
    -- CE 2.2.1 SetInput pads its Lua stack to four, then reads port from
    -- argument three. Two-argument calls both address port zero!
    for port=0,1 do
        emu.setInput(held[port+1],0,port,0)
        local actual=emu.getInput(port,0)
        for _,key in ipairs({"up","down","left","right","one","two","pause"}) do
            assert(actual[key]==held[port+1][key],"Input readback mismatch: "..port.." "..key)
        end
    end
end
input()
emu.addEventCallback(input,emu.eventType.inputPolled)
local frame=0
local pending=nil
local boundaries=assert(io.open(cfg.out.."boundaries.txt","wb"))
emu.addMemoryCallback(function()
    if not pending then return end
    -- Debugger::ProcessInstruction dispatches Lua Exec before ReadOpCode;
    -- operand/fetch notifications do not dispatch a second Lua Exec.
    local prefix=pending;pending=nil
    boundaries:write(string.format("%d %d\n",frame,emu.getCpuCycleCount(emu.cpuType.sms)))
    for _,d in ipairs({{"ram","smsWorkRam",8192},{"vram","smsVideoRam",16384},{"cram","smsPaletteRam",32},{"cart-ram","smsCartRam",8192}}) do
        put(prefix..d[1]..".bin",bytes(d[2],d[3]))
    end
    if frame==cfg.frames then boundaries:close();put("complete.txt",tostring(frame));emu.stop(0) end
end,emu.callbackType.exec,0,65535,emu.cpuType.sms,emu.memType.smsMemory)
emu.addEventCallback(function()
    frame=frame+1
    local rgb=emu.getScreenBuffer();local t={}
    for i=24*256+1,216*256 do local v=rgb[i];t[#t+1]=string.char(((v>>22)&3)|(((v>>14)&3)<<2)|(((v>>6)&3)<<4)) end
    local prefix=string.format("frame-%06d.",frame)
    put(prefix.."picture.bin",table.concat(t))
    pending=prefix
    held=cfg.inputs[frame];input()
end,emu.eventType.startFrame)
