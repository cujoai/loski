local network = require "network"
local utils = require "test.utils"

do
	local file1 = os.tmpname()
	local file2 = os.tmpname()
	assert(os.remove(file1))
	assert(os.remove(file2))

	local addr1 = network.address("file", file1)
	local addr2 = network.address("file", file2)
	local dgrm1 = network.socket("datagram", "file")
	local dgrm2 = network.socket("datagram", "file")
	assert(dgrm1:bind(addr1))
	assert(dgrm2:bind(addr2))

	assert(dgrm1:send("Hello", 1, -1, addr2))
	local from = network.address("file")
	local data = assert(dgrm2:receive(5, "", from))
	assert(data == "Hello")
	assert(from == addr1)

	assert(dgrm2:connect(addr1))
	assert(dgrm2:send("Hi"))
	assert(dgrm1:receive(2) == "Hi")

	assert(os.remove(file1))
	assert(os.remove(file2))
end

do
	local file = os.tmpname()
	assert(os.remove(file))

	local addr = network.address("file", file)
	local server = network.socket("listen", "file")
	assert(server:bind(addr))
	assert(server:listen())

	local conn = network.socket("stream", "file")
	assert(conn:connect(addr))

	local accepted = assert(server:accept())

	assert(conn:send("Hello"))
	local data = assert(accepted:receive(5))
	assert(data == "Hello")

	assert(accepted:send("Hi"))
	assert(conn:receive(2) == "Hi")

	assert(os.remove(file))
end
