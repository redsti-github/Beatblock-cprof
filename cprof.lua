jit.off()
cprof = package.loadlib(love.filesystem.getSaveDirectory() .. "/Mods/cprof/cprof.so", "luaopen_cprof")()

cprof.functionNames = {}
cprof.frameCount = 0
local unknownFunctionNames = {}
local updateFunctionNames = false

local function finfo(f)
	if cprof.functionNames[f] then
		return cprof.functionNames[f] -- TODO: tooltip that shows "file[line:line]"
	else
		if not unknownFunctionNames[f] then
			updateFunctionNames = true
			unknownFunctionNames[f] = 0
		end
	end

	local info = debug.getinfo(f, "S")
	local res = ""
	if info.source then
		if string.sub(info.source,1,1) == '@' or string.sub(info.source,1,1) == '=' then
			res = res .. info.source
		else
			res = res .. "<from string>" -- TODO: show the code on hover (tooltip)
		end
	end
	res = res .. "[" .. tostring(info.linedefined) .. ":" .. tostring(info.lastlinedefined)  .. "]"

	return res
end

local function scanForGlobalFunctions()
	local scanned = {}
	scanned[_G] = true
	local toScan = {{t=_G,p=""}}

	while #toScan > 0 do
		local t = toScan[#toScan]
		toScan[#toScan] = nil
		local path = t.p
		local t = t.t

		for k,v in pairs(t) do
			if type(k) ~= "string" then goto continue end
			if type(v) == "function" and unknownFunctionNames[v] then
				-- figure out what name to give this function
				local name = path .. k
				if prof.pop == prof.push and v == prof.pop then
					name = "prof.noop"
				end

				-- check for conflicts (debug)
				if cprof.functionNames[v] and cprof.functionNames[v] ~= name then
					print("conflict: ", cprof.functionNames[v], name)
				end

				-- apply name
				cprof.functionNames[v] = name

				-- mark as done
				unknownFunctionNames[v] = nil
			elseif type(v) == "table" and not scanned[v] then
				scanned[v] = true
				toScan[#toScan+1] = {t=v, p=path .. k .. "."}
			end
			::continue::
		end
	end
end

local function drawProfTable(root)
	local subcalls = {}
	local selfTime = root.t
	local func = root.f

	for func,next in pairs(root) do
		if func == "t" then goto continue end
		if func == "p" then goto continue end
		if func == "f" then goto continue end

		table.insert(subcalls, next)
		selfTime = selfTime - next.t

		::continue::
	end

	-- sort by time taken
	table.sort(subcalls, function(a, b) return a.t > b.t end)

	-- print
	local text = string.format("%.2fms  %s", root.t/1000 / cprof.frameCount, finfo(func))
	if #subcalls == 0 then
		imgui.BulletText(text)
	elseif imgui.TreeNode_StrStr(tostring(func), "%s", text) then
		if #subcalls > 0 then
			imgui.BulletText(string.format("%.2fms  (self)", selfTime/1000 / cprof.frameCount))
		end

		for _,v in ipairs(subcalls) do
			drawProfTable(v)
		end

		imgui.TreePop()
	end
end

function cprof.draw()
	cprof.stop()
	local root = cprof.getProfTable()
	cprof.frameCount = cprof.frameCount + 1
	if root ~= {} then
		helpers.SetNextWindowPos(0, 25, 'ImGuiCond_FirstUseEver')
		helpers.SetNextWindowSize(250, 600, 'ImGuiCond_FirstUseEver')
		imgui.Begin("Profiler info")

		imgui.Text(string.format("Proftime: %.2f ms total", cprof.getProfTime()/1000 / cprof.frameCount))
		local doReset = imgui.Button("reset");

		local size =  imgui.GetContentRegionAvail()
		if imgui.BeginListBox("##palette", size) then
			for k,v in pairs(root) do
				drawProfTable(v)
			end
			imgui.EndListBox()
		end
		imgui.End()

		if updateFunctionNames then
			scanForGlobalFunctions(_G, "")
			updateFunctionNames = false
		end

		if doReset then -- or cprof.frameCount > 15
			cprof.reset()
			cprof.frameCount = 0
		end
	end
	cprof.start()
end

return cprof
