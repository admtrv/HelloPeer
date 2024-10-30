-- Add this protocol file to '/Wireshark/plugins/<version>/'

-- Creating custom protocol
tcu_proto = Proto("tcu", "Transmission Control over UDP Protocol")

-- Creating fields
local fields = tcu_proto.fields
fields.seq_num = ProtoField.uint24("tcu.seq_num", "Sequence Number", base.DEC)
fields.flags = ProtoField.uint8("tcu.flags", "Flags", base.HEX)
fields.length = ProtoField.uint16("tcu.length", "Payload Length", base.DEC)
fields.checksum = ProtoField.uint16("tcu.checksum", "Checksum", base.HEX)

-- Flags definitions
local SYN  = 0x01
local ACK  = 0x02
local FIN  = 0x04
local NACK = 0x08
local DF   = 0x10
local MF   = 0x20
local FL   = 0x40
local KA   = 0x80

-- Function to parse protocol
function tcu_proto.dissector(buffer, pinfo, tree)
    pinfo.cols.protocol = "TCU"

    -- Checking length (8 bytes)
    if buffer:len() < 8 then 
    	return
    end

    -- Adding protocol tree
    local subtree = tree:add(tcu_proto, buffer(), "Transmission Control over UDP Protocol")

    local offset = 0

    -- Sequence Number (3 bytes)
    local seq_num_field = buffer(offset, 3)
    local seq_num = seq_num_field:uint()
    subtree:add(fields.seq_num, seq_num_field)
    offset = offset + 3

    -- Flags (1 byte)
    local flags_field = buffer(offset, 1)
    local flags_val = flags_field:uint()
    local flags_str_list = {}
    local function has_flag(flag)
        return bit.band(flags_val, flag) > 0
    end
    if has_flag(SYN) then table.insert(flags_str_list, "SYN") end
    if has_flag(ACK) then table.insert(flags_str_list, "ACK") end
    if has_flag(FIN) then table.insert(flags_str_list, "FIN") end
    if has_flag(NACK) then table.insert(flags_str_list, "NACK") end
    if has_flag(DF) then table.insert(flags_str_list, "DF") end
    if has_flag(MF) then table.insert(flags_str_list, "MF") end
    if has_flag(FL) then table.insert(flags_str_list, "FL") end
    if has_flag(KA) then table.insert(flags_str_list, "KA") end
    if #flags_str_list == 0 then
        table.insert(flags_str_list, "NO FLAG")
    end
    local flags_str = table.concat(flags_str_list, ", ")
    subtree:add(fields.flags, flags_field):append_text(" (" .. flags_str .. ")")
    offset = offset + 1

    -- Length (2 bytes)
    local length_field = buffer(offset, 2)
    local length = length_field:uint()
    subtree:add(fields.length, length_field)
    offset = offset + 2

    -- Checksum (2 bytes)
    local checksum_field = buffer(offset, 2)
    local checksum = checksum_field:uint()
    subtree:add(fields.checksum, checksum_field)
    offset = offset + 2

    -- Determine packet type
    local info_str = string.format("%d → %d ", pinfo.src_port, pinfo.dst_port)

    if has_flag(SYN) and has_flag(ACK) then
        info_str = info_str .. "Connection Acknowledgment"
    elseif has_flag(SYN) then
        info_str = info_str .. "Connection Request"
    elseif has_flag(FIN) and has_flag(ACK) and length == 0 then
        info_str = info_str .. "Disconnection Acknowledgment"
    elseif has_flag(FIN) and length == 0 then
        info_str = info_str .. "Disconnection Request"
    elseif has_flag(KA) and has_flag(ACK) and length == 0 then
        info_str = info_str .. "Keep-Alive Acknowledgment"
    elseif has_flag(KA) and length == 0 then
        info_str = info_str .. "Keep-Alive Request"
    elseif has_flag(ACK) and length == 0 then
        info_str = info_str .. "Positive Acknowledgment " .. tostring(seq_num)
    elseif has_flag(NACK) and length == 0 then
        info_str = info_str .. "Negative Acknowledgment " .. tostring(seq_num)
    elseif has_flag(DF) and has_flag(FL) then
        info_str = info_str .. "Single File Message"
    elseif has_flag(DF) then
        info_str = info_str .. "Single Text Message"
    elseif has_flag(MF) and has_flag(FL) then
        info_str = info_str .. "Fragment of File Message " .. tostring(seq_num)
    elseif has_flag(MF) then
        info_str = info_str .. "Fragment of Text Message " .. tostring(seq_num)
    elseif has_flag(FL) and not (has_flag(DF) or has_flag(MF)) then
        info_str = info_str .. "Last Fragment of File Message " .. tostring(seq_num)
    elseif not (has_flag(DF) or has_flag(MF) or has_flag(FL)) then
        info_str = info_str .. "Last Fragment of Text Message " .. tostring(seq_num)
    else
        info_str = info_str .. "Unknown Packet Type"
    end
    -- Update info 
    pinfo.cols.info:set(info_str)
    
end

-- Registering protocol over UDP port
local udp_port = DissectorTable.get("udp.port")
-- Add here your ports
udp_port:add(5000, tcu_proto)
udp_port:add(5001, tcu_proto)