-- SPDX-License-Identifier: GPL-3.0-or-later
-- Generated config contains only paths and authored button states.
local cfg=dofile([[CONFIG_PATH]])
-- CE logs callback errors and can keep running to a successful stop later.
-- Make any failed callback fatal, with a diagnostic the launcher can preserve.
local failed=false
local function guard(callback)
    return function(...)
        if failed then return end
        local ok,message=xpcall(callback,tostring,...)
        if not ok then
            failed=true
            local f=io.open(cfg.out.."capture-error.txt","wb")
            if f then f:write(message);f:close() end
            emu.stop(1)
        end
    end
end
local addMemoryCallback=emu.addMemoryCallback
emu.addMemoryCallback=function(callback,...) return addMemoryCallback(guard(callback),...) end
local addEventCallback=emu.addEventCallback
emu.addEventCallback=function(callback,...) return addEventCallback(guard(callback),...) end
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
local writes={}
local queued_vdp=nil
local psg_writes={}
local boundary_state=nil
-- CE does not notify Lua of VRAM stores in WriteVram (CRAM stores do notify).
-- Journal the shared pending CPU write using CE's SMS1 slot schedule. This
-- matters when its transfer completes during IRQ entry, after the wanted E.
-- Coordinates below are completion dots, one beyond CE's bus index.
local function next_slot(cycle,enabled)
    local absolute=(2+3*cycle)//2+1
    while true do
        local line=(absolute//342)%262
        local dot=absolute%342
        local bus=dot-1
        local available=false
        if dot%2==1 then
            if not enabled then available=bus>=256 or bus%4==2
            elseif line>=192 and line~=261 then available=not(line==260 and bus>=326) and (bus>=256 or bus%4==2)
            elseif bus<256 then available=line<192 and bus%32==2 or line==261 and (bus%8~=2 or bus&24==0)
            elseif bus<264 then available=true
            else available=bus>=288 and bus<=296 or bus==322 or bus==324 end
        end
        if available then return (2*absolute-2+2)//3 end
        absolute=absolute+1
    end
end
local function flush_vdp(cycle)
    if queued_vdp and queued_vdp.cycle<=cycle then
        if pending then writes[#writes+1]=queued_vdp end
        queued_vdp=nil
    end
end
local boundaries=assert(io.open(cfg.out.."boundaries.txt","wb"))
local function capture(cycle,undo_after)
    if not pending then return end
    flush_vdp(emu.getCpuCycleCount(emu.cpuType.sms))
    local prefix=pending;pending=nil
    boundaries:write(string.format("%d %d\n",frame,cycle))
    local state=emu.getState()
    -- An interrupt event is delivered after entry. Restore the changed
    -- registers in the exported snapshot, using the interrupt stack and the
    -- values observed at frame start; the emulator itself is not modified.
    if undo_after then
        local sp=state["cpu.sp"]
        state["cpu.pc"]=emu.read(sp,emu.memType.smsMemory)|(emu.read((sp+1)&65535,emu.memType.smsMemory)<<8)
        state["cpu.sp"]=(sp+2)&65535
        state["cpu.r"]=(state["cpu.r"]&128)|((state["cpu.r"]-1)&127)
        state["cpu.halted"]=boundary_state["cpu.halted"]
        state["cpu.iff1"]=undo_after.nmi and boundary_state["cpu.iff1"] or true
        if not undo_after.nmi then state["cpu.iff2"]=boundary_state["cpu.iff2"] end
        undo_after=undo_after.cycle
    end
    local fields={"a","flags","b","c","d","e","h","l","altA","altFlags","altB","altC","altD","altE","altH","altL","ixh","ixl","iyh","iyl"}
    local output={}
    for _,key in ipairs(fields) do local v=assert(state["cpu."..key],"CPU register unavailable: "..key); output[#output+1]=string.char(v) end
    for _,key in ipairs({"sp","pc"}) do local v=assert(state["cpu."..key],"CPU register unavailable: "..key); output[#output+1]=string.char(v>>8,v&255) end
    for _,key in ipairs({"i","r","iff1","iff2","im","halted"}) do
        local v=state["cpu."..key]; assert(v~=nil,"CPU register unavailable: "..key)
        if type(v)=="boolean" then v=v and 1 or 0 end
        output[#output+1]=string.char(v)
    end
    put(prefix.."cpu.bin",table.concat(output))
    put(prefix.."psg.bin",table.concat(psg_writes[frame-1] or {}));psg_writes[frame-1]=nil
    for _,d in ipairs({{"ram","smsWorkRam",8192},{"vram","smsVideoRam",16384},{"cram","smsPaletteRam",32},{"cart-ram","smsCartRam",8192}}) do
        local data=bytes(d[2],d[3])
        -- CE dispatches IRQ/NMI events after entry. Undo writes belonging to
        -- that entry in the exported copy only; never modify the emulator.
        if undo_after then
            for i=#writes,1,-1 do
                local w=writes[i]
                if w.cycle>undo_after and w.domain==emu.memType[d[2]] and w.address<d[3] then
                    data=data:sub(1,w.address)..string.char(w.old)..data:sub(w.address+2)
                end
            end
        end
        put(prefix..d[1]..".bin",data)
    end
    if frame==cfg.frames then boundaries:close();put("complete.txt",tostring(frame));emu.stop(0) end
end
emu.addMemoryCallback(function()
    -- The next exec is suitable only if no interrupt entry followed the
    -- completed instruction. Interrupt events below capture that case first.
    capture(emu.getCpuCycleCount(emu.cpuType.sms))
end,emu.callbackType.exec,0,65535,emu.cpuType.sms,emu.memType.smsMemory)
local function remember(address,domain)
    if not pending then return end
    writes[#writes+1]={cycle=emu.getCpuCycleCount(emu.cpuType.sms),domain=domain,address=address,old=emu.read(address,domain)}
end
emu.addMemoryCallback(function(address)
    if not pending then return end
    local absolute=emu.convertAddress(address,emu.memType.smsMemory,emu.cpuType.sms)
    if absolute then remember(absolute.address,absolute.memType) end
end,emu.callbackType.write,0,65535,emu.cpuType.sms,emu.memType.smsMemory)
local memory_write_cycle=-1
emu.addMemoryCallback(function() memory_write_cycle=emu.getCpuCycleCount(emu.cpuType.sms) end,
    emu.callbackType.write,0,65535,emu.cpuType.sms,emu.memType.smsMemory)
emu.addMemoryCallback(function(port,value)
    local cycle=emu.getCpuCycleCount(emu.cpuType.sms)
    -- CE loses the port domain when converting absolute callbacks. A prior
    -- relative memory notification excludes real writes at these ROM aliases.
    if cycle==memory_write_cycle then return end
    flush_vdp(cycle)
    if not pending and cycle%59736<59704 then return end
    if port<0x80 or port>=0xc0 then return end
    local s=emu.getState()
    local address=s["vdp.addressReg"]
    local enabled=s["vdp.renderingEnabled"]
    if port&1==0 then
        local domain=s["vdp.codeReg"]==3 and emu.memType.smsPaletteRam or emu.memType.smsVideoRam
        local index=address%(domain==emu.memType.smsPaletteRam and 32 or 16384)
        queued_vdp={cycle=next_slot(cycle,enabled),domain=domain,address=index,old=emu.read(index,domain)}
    elseif queued_vdp then
        if s["vdp.controlPortMsbToggle"] then
            address=(address&255)|((value&63)<<8)
            if value>>6==2 and value&15==1 then enabled=address&0x40~=0 end
        else address=(address&0x3f00)|value end
        local domain=queued_vdp.domain
        local index=address%(domain==emu.memType.smsPaletteRam and 32 or 16384)
        queued_vdp={cycle=next_slot(cycle,enabled),domain=domain,address=index,old=emu.read(index,domain)}
    end
end,emu.callbackType.write,0x80,0xbf,emu.cpuType.sms,emu.memType.smsPrgRom)
emu.addMemoryCallback(function(port,value)
    local cycle=emu.getCpuCycleCount(emu.cpuType.sms)
    if cycle==memory_write_cycle then return end
    -- Absolute callbacks also alias VDP memory writes to ROM addresses.
    -- Those fire inside VDP Exec, before its current dot is incremented;
    -- a CPU port write fires only after ExecCycles has synchronized the VDP.
    local state=emu.getState()
    local dot=state["vdp.scanline"]*342+state["vdp.cycle"]
    if dot~=(1+(3*cycle)//2)%(342*262) then return end
    local index=cycle//59736
    local offset=cycle%59736
    psg_writes[index]=psg_writes[index] or {}
    psg_writes[index][#psg_writes[index]+1]=string.char(offset>>8,offset&255,value)
end,emu.callbackType.write,0x40,0x7f,emu.cpuType.sms,emu.memType.smsPrgRom)
for _,domain in ipairs({emu.memType.smsVideoRam,emu.memType.smsPaletteRam}) do
    emu.addMemoryCallback(function(address) remember(address,domain) end,emu.callbackType.write,0,domain==emu.memType.smsVideoRam and 16383 or 31,emu.cpuType.sms,domain)
end
local function interrupt(nmi)
    if not pending then return end
    local state=emu.getState()
    local duration=nmi and 11 or (state["cpu.im"]==2 and 19 or 13)
    local ending=emu.getCpuCycleCount(emu.cpuType.sms)-duration
    -- A frame reached during entry itself belongs to the completed entry,
    -- not to the instruction that ended before the frame boundary.
    if ending>=frame*59736 then capture(ending,{cycle=ending,nmi=nmi}) end
end
emu.addEventCallback(function() interrupt(false) end,emu.eventType.irq)
emu.addEventCallback(function() interrupt(true) end,emu.eventType.nmi)
emu.addEventCallback(function()
    frame=frame+1
    boundary_state=emu.getState()
    local rgb=emu.getScreenBuffer();local t={}
    for i=24*256+1,216*256 do local v=rgb[i];t[#t+1]=string.char(((v>>22)&3)|(((v>>14)&3)<<2)|(((v>>6)&3)<<4)) end
    local prefix=string.format("frame-%06d.",frame)
    put(prefix.."picture.bin",table.concat(t))
    pending=prefix
    writes={}
    -- Drop transfers known to have completed before the frame began.
    if queued_vdp and queued_vdp.cycle<frame*59736 then queued_vdp=nil end
    held=cfg.inputs[frame];input()
    -- A halted CPU has no opcode-fetch callbacks. Its visible registers do
    -- not change during CE's halted four-cycle step; CycleCount already
    -- names that step's end when the VDP reaches this boundary.
    if boundary_state["cpu.halted"] then capture(emu.getCpuCycleCount(emu.cpuType.sms)) end
end,emu.eventType.startFrame)
