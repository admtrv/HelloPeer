-- Add this protocol file to '/Wireshark/plugins/<version>/'

-- Creating custom protocol
tcu_proto = Proto("tcu", "Transmission Control over UDP Protocol")

-- Creating fields
local fields = tcu_proto.fields
fields.length = ProtoField.uint16("tcu.length", "Payload Length", base.DEC)
fields.flags = ProtoField.uint16("tcu.flags", "Flags", base.HEX)
fields.seq_num = ProtoField.uint16("tcu.seq_num", "Sequence Number", base.DEC)
fields.checksum = ProtoField.uint16("tcu.checksum", "Checksum", base.HEX)

-- Creating flags
local FLAGS = {
    [0x01] = "SYN",
    [0x02] = "ACK",
    [0x04] = "FIN",
    [0x08] = "NACK",
    [0x10] = "DF",
    [0x20] = "MF",
    [0x40] = "FL",
    [0x80] = "KA"
}

-- Function to parse protocol
function tcu_proto.dissector(buffer, pinfo, tree)
    pinfo.cols.protocol = "TCU"

    -- Checking length (8 bytes)
    if buffer:len() < 8 then 
    	return 
    end

    -- Adding protocol tree
    local subtree = tree:add(tcu_proto, buffer(), "Transmission Control over UDP Protocol")

    -- Length
    local length = buffer(0, 2):uint()
    subtree:add(fields.length, buffer(0, 2))
        
    -- Flags
    local flags_field = buffer(2, 2)
    local flags_val = flags_field:uint()
    local flags_str = {}
    for flag, name in pairs(FLAGS) do
        if bit.band(flags_val, flag) > 0 then
            table.insert(flags_str, name)
        end
    end
    if #flags_str == 0 then
    	   table.insert(flags_str, "NO FLAG")
    end
    subtree:add(fields.flags, flags_field):append_text(" (" .. table.concat(flags_str, ", ") .. ")")

    -- Sequence number
    local seq_num = buffer(4, 2):uint()
    subtree:add(fields.seq_num, buffer(4, 2))
    
    -- Checksum
    local checksum = buffer(6, 2):uint()
    subtree:add(fields.checksum, buffer(6, 2))
    
    -- Determine packet type 
    local info_str = string.format("%d -> %d ", pinfo.src_port, pinfo.dst_port)
    
    if bit.band(flags_val, 0x01) > 0 and bit.band(flags_val, 0x02) > 0 then
        info_str = info_str .. "Connection Acknowledgment"
    elseif bit.band(flags_val, 0x01) > 0 then
        info_str = info_str .. "Connection Request"
    elseif bit.band(flags_val, 0x04) > 0 and bit.band(flags_val, 0x02) > 0 then
        info_str = info_str .. "Disconnection Acknowledgment"
    elseif bit.band(flags_val, 0x04) > 0 then
        info_str = info_str .. "Disconnection Request"
    elseif bit.band(flags_val, 0x80) > 0 and bit.band(flags_val, 0x02) > 0 then
        info_str = info_str .. "Keep-Alive Acknowledgment"
    elseif bit.band(flags_val, 0x80) > 0 then
        info_str = info_str .. "Keep-Alive Request"
    elseif bit.band(flags_val, 0x10) > 0 and bit.band(flags_val, 0x40) == 0 then
        info_str = info_str .. "Single Text Message"
    elseif bit.band(flags_val, 0x10) > 0 and bit.band(flags_val, 0x40) > 0 then
        info_str = info_str .. "Single File Message"
    elseif bit.band(flags_val, 0x20) > 0 and bit.band(flags_val, 0x40) == 0 then
        info_str = info_str .. "Fragment of Text Message " .. tostring(seq_num)
    elseif bit.band(flags_val, 0x20) > 0 and bit.band(flags_val, 0x40) > 0 then
        info_str = info_str .. "Fragment of File Message " .. tostring(seq_num)
    elseif flags_val == 0 and bit.band(flags_val, 0x40) == 0 then
        info_str = info_str .. "Last Fragment of Text Message " .. tostring(seq_num)
    elseif flags_val == 0 and bit.band(flags_val, 0x40) > 0 then
        info_str = info_str .. "Last Fragment of File Message " .. tostring(seq_num)
    elseif bit.band(flags_val, 0x08) > 0 then
        info_str = info_str .. "Negative Acknowledgment " .. tostring(seq_num)
    elseif bit.band(flags_val, 0x02) > 0 then
        info_str = info_str .. "Acknowledgment"
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