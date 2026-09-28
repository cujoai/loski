local tests = require "test.process.utils"

tests.runscript([==[
local process = require "process"
local time = require "time"

local NCHILDREN = 10

local mypid = assert(tonumber(io.open("/proc/self/stat"):read("a"):match("^(%d+)")))

local function count_zombies()
	local children = io.open(string.format("/proc/%d/task/%d/children", mypid, mypid)):read("a")
	local count = 0
	for pid in children:gmatch("%d+") do
		local stat = io.open("/proc/"..pid.."/stat"):read("a")
		if stat and stat:match("^%d+ %b() Z") then count = count+1 end
	end
	return count
end

local function sh(script)
	return assert(process.create{execfile = "/bin/sh", arguments = { "-c", script }})
end

-- Stop us from t=0.2 to t=1.2 while the children all exit at t=0.5.
local helper = sh(string.format("sleep 0.2; kill -STOP %d; sleep 1; kill -CONT %d", mypid, mypid))
local procs = {}
for i = 1, NCHILDREN do
	procs[i] = sh("sleep 0.5; exit "..i)
end

-- Check that each child gets a reported exit status.
local deadline = time.now() + 2
local pending
repeat
	time.sleep(0.1)
	pending = 0
	for i, proc in ipairs(procs) do
		local exitval, err = process.exitval(proc)
		if exitval == nil then
			assert(err == "unfulfilled", err)
			pending = pending + 1
		else
			assert(exitval == i, exitval)
		end
	end
until (process.status(helper) == "dead" and pending == 0) or time.now() > deadline
assert(pending == 0)

-- Start another child that doesn't exit immediately and GC its handle
sh("sleep 0.2")
collectgarbage()

-- Call into loski after the child exits: it should get reaped here
time.sleep(0.3)
process.status(sh("exit 0"))

local orphaned = count_zombies()
assert(orphaned == 0)
]==])
