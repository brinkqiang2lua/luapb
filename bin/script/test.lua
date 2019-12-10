-- lua script
require("luapb")
local luapb = pb.new("net.proto")

function bin2hex(s)
    s = string.gsub(s,"(.)",function (x) return string.format("%02X ",string.byte(x)) end)
    return s
end

function pb_encode_test(num) 
    local message = {
        number = "13615632545",
        email = "13615632545@163.com",
        age = 28,
        ptype = "WORK",
        desc = {"first", "second", "three"},
        jobs = {
            {
                jobtype = 8345,
                jobdesc = "coder"
            },
            {
                jobtype = 9527,
                jobdesc = "coder2"
            }
        }
    }

    local t1 = os.clock();
    for i=1,num do
        local buffer = luapb:encode("net.tb_Person", message)
    end 

    print("num=".. num .."\ttime="..os.clock()-t1)

    print("pb_encode_test pass #\n" )
end

function pb_decode_test(num) 
    local message = {
        number = "13615632545",
        email = "13615632545@163.com",
        age = 28,
        ptype = "WORK",
        desc = {"first", "second", "three"},
        jobs = {
            {
                jobtype = 8345,
                jobdesc = "coder"
            },
            {
                jobtype = 9527,
                jobdesc = "coder2"
            }
        }
    }
    local buffer = luapb:encode("net.tb_Person", message)

    local t1 = os.clock();
    for i=1,num do
        local msg = luapb:decode("net.tb_Person", buffer)
    end 

    print("num=".. num .."\ttime="..os.clock()-t1)
    
    --print("pb_table_test:  " .. bin2hex(buffer))

    print("pb_decode_test pass #\n" )
end

pb_encode_test(1000000)
pb_decode_test(1000000)
