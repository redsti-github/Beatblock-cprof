jit.off()

cprof = package.loadlib(love.filesystem.getSaveDirectory() .. "/Mods/cprof/cprof.so", "luaopen_cprof")()

local PRINT_MIN_MS = 0.05 -- minimum time a function needs to take to be printed

local function finfo(f)
	local info = debug.getinfo(f, "S")
	local res = ""
	if info.source then
		if string.sub(info.source,1,1) == '@' or string.sub(info.source,1,1) == '=' then
			res = res .. info.source
		else
			res = res .. "<from string>"
		end
	end
	res = res .. "[" .. tostring(info.linedefined) .. ":" .. tostring(info.lastlinedefined)  .. "]"

	return res
end

local function printProfTable(func, root, depth, maxdepth)
	maxdepth = maxdepth or 30
	depth = depth or 0

	local tab = "|  "
	local tabs = ""
	for i=0,depth do tabs = tabs .. tab end

	local subcalls = {}
	local selfTime = root.t
	for func,next in pairs(root) do
		if func == "t" then goto continue end
		if func == "p" then goto continue end

		if next.t/1000 > PRINT_MIN_MS then
			table.insert(subcalls, {func=func, time=next.t, next=next})
		end
		selfTime = selfTime - next.t

		::continue::
	end

	--if #subcalls == 1 and depth == 0 then -- only one root call, skip it for clarity TODO: print on one line instead?
	--	return printProfTable(subcalls[1].next, depth, maxdepth)
	--end

	-- sort by time taken
	table.sort(subcalls, function(a, b) return a.time > b.time end)

	-- print
	print(tabs .. tostring(root.t/1000) .. "ms  " .. finfo(func))

	if #subcalls > 0 then
		if selfTime/1000 > PRINT_MIN_MS then
			print(tabs .. tab .. tostring(selfTime/1000) .. "ms  (self)")
		end
		if depth >= maxdepth then
			print(tabs .. tab .. "...")
		else
			for _,v in ipairs(subcalls) do
				printProfTable(v.func, v.next, depth+1, maxdepth)
			end
		end
	end
end

local function drawProfTable(func, root)
	local PRINT_MIN_MS = 0

	local subcalls = {}
	local selfTime = root.t
	for func,next in pairs(root) do
		if func == "t" then goto continue end
		if func == "p" then goto continue end

		if next.t/1000 > PRINT_MIN_MS then
			table.insert(subcalls, {func=func, time=next.t, next=next})
		end
		selfTime = selfTime - next.t

		::continue::
	end

	-- sort by time taken
	table.sort(subcalls, function(a, b) return a.time > b.time end)

	-- print
	local text = string.format("%.2fms  %s", root.t/1000, finfo(func))
	if #subcalls == 0 then
		imgui.BulletText(text)
	elseif imgui.TreeNode_StrStr(tostring(func), "%s", text) then
		if #subcalls > 0 and selfTime/1000 > PRINT_MIN_MS then
			imgui.BulletText(string.format("%.2fms  (self)", selfTime/1000))
		end

		for _,v in ipairs(subcalls) do
			drawProfTable(v.func, v.next)
		end

		imgui.TreePop()
	end
end

function cprof.resetPrint()
	cprof.stop()
	local root = cprof.getProfTable()
	if root ~= {} then
		helpers.SetNextWindowPos(0, 25, 'ImGuiCond_FirstUseEver')
		helpers.SetNextWindowSize(250, 600, 'ImGuiCond_FirstUseEver')
		imgui.Begin("Profiler info")

		local size =  imgui.GetContentRegionAvail()
		if imgui.BeginListBox("##palette", size) then
			for k,v in pairs(root) do
				drawProfTable(k,v)
			end
			imgui.EndListBox()
		end
		imgui.End()

		--print("START")
		--for k,v in pairs(root) do
		--	printProfTable(k,v)
		--end
		--print("END")
	end
end

return cprof
