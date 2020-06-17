local network = require "network"
local memory = require "memory"

local tests = require "test.utils"

do
	tests.IsWindows = os.getenv("PATHEXT")~=nil
	tests.LocalHost = "127.0.0.1"
	tests.FreeAddress = network.address("ipv4", tests.LocalHost, 43210)
	tests.UsedAddress = network.address("ipv4", tests.LocalHost, 54321)
	tests.DeniedAddress = network.address("ipv4", tests.LocalHost, 1)
	tests.LocalAddress = network.address("ipv4", tests.LocalHost, 43212)
	tests.RemoteTCP = network.address("ipv4", "8.8.8.8", 53)
	tests.OtherTCP = network.address("ipv4", "8.8.4.4", 53)
	tests.RemoteUDP = tests.RemoteTCP
end

do
	local socket = assert(network.socket("listen"))
	local res, errmsg = socket:bind(tests.UsedAddress)
	assert(res == true or errmsg == "address used")
	tests.BindedSocket = socket -- avoid garbage collection
end

do
	local process = require "process"

	local luabin = "lua"
	do
		local i = -1
		while arg[i] ~= nil do
			luabin = arg[i]
			i = i-1
		end
	end

	function tests.newprocess(code)
		return assert(process.create(luabin, "-e", code))
	end
end

do
	local time = require "time"

	function tests.tcall(unfulfilled, func, ...)
		for i = 1, 10 do
			local results = table.pack(func(...))
			local res, errmsg = results[1], results[2]
			if res ~= nil or errmsg ~= "unfulfilled" then
				if unfulfilled then assert(res == nil or i > 1) end
				return table.unpack(results, 1, results.n)
			end
			time.sleep(.1)
		end
		error("non blocked call took more than 1 second to complete!")
	end
end

do
	function tests.testcreatesocket(kind, domain)
		local socket = assert(network.socket(kind, domain))
		assert(type(socket) == "userdata")
		assert(socket.type == kind)
		assert(socket:getdomain() == domain or "ipv4")
		assert(string.match(tostring(socket), "^socket %(.-%)$"))
		return socket
	end
end

do
	local options = {
		blocking = true,
		reuseaddr = false,
		dontroute = false,
		nodelay = false,
		keepalive = false,
		linger = 0, -- actually is 'nil'
		broadcast = false,
	}
	local changed = {
		[true] = false,
		[false] = true,
		[0] = 10,
	}
	local disallowed = {
		listen = {
			nodelay = true,
			keepalive = true,
			linger = true,
			broadcast = true,
		},
		stream = {
			broadcast = true,
		},
		datagram = {
			nodelay = true,
			keepalive = true,
			linger = true,
		},
	}
	function tests.testoptions(socket, kind)
		local disallowed = disallowed[kind]
		for name, default in pairs(options) do
			if disallowed[name] then
				local errmsg = "bad argument #2 to '?' (invalid option '"..name.."')"
				tests.testerror(errmsg, socket.getoption, socket, name)
				local errmsg = "bad argument #2 to '?' (invalid option '"..name.."')"
				tests.testerror(errmsg, socket.setoption, socket, name, changed[default])
			else
				if name == "linger" then
					assert(socket:getoption(name) == nil)
					assert(socket:setoption(name, 0) == true)
					assert(socket:getoption(name) == 0)
					assert(socket:setoption(name, 10) == true)
					assert(socket:getoption(name) == 10)
					assert(socket:setoption(name, nil) == true)
					local errmsg = "bad argument #3 to '?' (number expected, got boolean)"
					tests.testerror(errmsg, socket.setoption, socket, name, false)
					assert(socket:getoption(name) == nil)
				else
					assert(socket:getoption(name) == default)
					assert(socket:setoption(name, changed[default]) == true)
					assert(socket:getoption(name) == changed[default])
					assert(socket:setoption(name, default) == true)
					assert(socket:getoption(name) == default)
				end
			end
		end
	end
end

do
	local ops = {"close", "bind", "getaddress", "getoption", "setoption"}
	function tests.testclose(socket)
		assert(socket:close() == true)
		for _, op in ipairs(ops) do
			local ok, errmsg = pcall(socket[op], socket)
			assert(not ok)
			assert(errmsg == "attempt to use a closed socket")
		end
	end
end

function tests.testreceive(socket, expected, ...)
	local size = #expected
	local buf = memory.create(size)
	assert(socket:receive(buf, 1, size, ...) == size)
	assert(memory.diff(buf, expected) == nil)
end

return tests
