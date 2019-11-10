-- "13662e2b83aaf678a0fd14ce3e6d5cba"

-- "&groupId=13662e2b83aaf678a0fd14ce3e6d5cba"
-- "&sobotFlag=3"
-- "&groupId=13662e2b83aaf678a0fd14ce3e6d5cba&sobotFlag=3"

function strOrNum(v)
    return tonumber(v) and v or "\""..v.."\""
end

function printT( t, n )
    n = n or 0
    local space = string.rep("\t", n)
    print(space.."{")
    for k,v in pairs(t) do
        if type(v) == "table" then
            printT(v, n+1)
        else
            print(string.format("\t%s[%s] = %s,", space, strOrNum(k), strOrNum(v)))
        end
    end
    print(space.."},")
end

function parseOpenext( openext )
    if not openext or openext == "" then
        return
    end
    local param = {}
    local eqIndex = string.find(openext, '=')
    if eqIndex then
        for k,v in string.gmatch(openext, "(%w+)=(%w+)") do
            param[k] = v
        end
    else
        param["groupId"] = openext
    end
    printT(param)
    return param
end

local tt = {nil, "", "test=3", "&groupId=abc", "&groupId=abc&sobotFlag=3", "sobotFlag=3", "&&", "="}
for k,v in pairs(tt) do
    parseOpenext(v)
end
