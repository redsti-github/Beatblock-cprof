jit.off()
cprof = package.loadlib(love.filesystem.getSaveDirectory() .. "/Mods/cprof/cprof.so", "luaopen_cprof")()

-- TODO: proper sorting
-- TODO: proper drawTimeMin
-- TODO: fix love.draw call count

cprof.functionNames = {}
cprof.frameCount = 0

local unknownFunctionNames = {}
local updateFunctionNames = false

local doProfile = false
local currentState = ""
local startOnStateChange = false
local stopOnStateChange = false

local timeFormat = "per frame avg"
local drawTimeMin = 0.2


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

local function simpletimef(time)
	time = time / 1000
	if time > 1000 then
		return string.format("%6.2f s ", time/1000)
	else
		return string.format("%6.2f ms", time)
	end
end

local function format(time, callcount, framecount, parenttime, totaltime, parentcallcount)
	if     timeFormat == 'total'          then return simpletimef(time) .. string.format(" [%i]", callcount)
	elseif timeFormat == 'per frame avg'  then return simpletimef(time / framecount) .. string.format(" [%.2f]", callcount / framecount)
	elseif timeFormat == 'per call avg'   then return simpletimef(time / callcount) .. string.format(" [%.2f]", callcount / parentcallcount)
	elseif timeFormat == 'percent'        then return string.format("%4.1f %% ", 100 * time / totaltime) .. string.format(" [%i]", callcount)
	elseif timeFormat == 'parent percent' then return string.format("%4.1f %% ", 100 * time / parenttime) .. string.format(" [%i]", callcount)
	else log("[cprof] error! 'timeFormat' is invalid!", "error") end
end

local function accuracyNote()
	imgui.SameLine()
	imgui.TextDisabled("(!)")

	if imgui.IsItemHovered() then
		imgui.SetNextWindowSize(imgui.ImVec2_Float(200, 0))
		if imgui.BeginTooltip() then
			imgui.TextWrapped("inaccurate; shown time is only an upper bound")
			imgui.EndTooltip()
		end
	end
end

local function drawProfTable(root, parenttime, totaltime, parentcallcount)
	local selfTime = root.t
	local func = root.f
	local accurate = true
	local callCount = root.c
	parenttime = parenttime or root.t
	totaltime = totaltime or root.t
	parentcallcount = parentcallcount or 1

	local subcalls = {}
	for func,next in pairs(root) do
		if func == "t" then goto continue end
		if func == "p" then goto continue end
		if func == "f" then goto continue end
		if func == "c" then goto continue end
		if func == "i" then accurate = false goto continue end

		--if next.t/1000 / cprof.frameCount > drawTimeMin then
			table.insert(subcalls, next)
		--end
		selfTime = selfTime - next.t

		::continue::
	end

	-- sort by time taken
	table.sort(subcalls, function(a, b) return a.t > b.t end)

	-- print
	local text = string.format("%s  %s", format(root.t, callCount, cprof.frameCount, parenttime, totaltime, parentcallcount), finfo(func))
	if #subcalls == 0 then
		imgui.BulletText(text)
		if not accurate then accuracyNote() end
	elseif imgui.TreeNode_StrStr(tostring(func), "%s", text) then
		if not accurate then accuracyNote() end
		--if selfTime/1000 / cprof.frameCount > drawTimeMin then
			imgui.BulletText(string.format("%.2fms  (self)", selfTime/1000 / cprof.frameCount))
		--end

		for _,v in ipairs(subcalls) do
			drawProfTable(v, root.t, totaltime, callCount)
		end

		imgui.TreePop()
	elseif not accurate then accuracyNote() end
end

local function imguiEnum(id, current, options)
	if imgui.BeginCombo(id, current) then
		for _, v in ipairs(options) do
			if imgui.Selectable_Bool(v, v == current) then
				current = v
			end
		end
		imgui.EndCombo()
	end
	return current
end





function cprof.draw()
	cprof.stop()
	local root = cprof.getProfTable()

	if root ~= {} then
		helpers.SetNextWindowPos(0, 25, 'ImGuiCond_FirstUseEver')
		helpers.SetNextWindowSize(250, 600, 'ImGuiCond_FirstUseEver')
		imgui.Begin("Profiler info")

		local doReset = false
		if imgui.BeginTable("##prof_control", 2) then
			imgui.TableNextRow();imgui.TableNextColumn()

			doProfile = helpers.InputBool('Enabled', doProfile)

			if doProfile then
				startOnStateChange = helpers.InputBool('Reset on new state', startOnStateChange)
			else
				startOnStateChange = helpers.InputBool('Enable on new state', startOnStateChange)
			end

			stopOnStateChange = helpers.InputBool('Stop on new state', stopOnStateChange)

			doReset = imgui.Button("reset");


			imgui.TableNextColumn();
			drawTimeMin = helpers.InputFloat("min draw ms", drawTimeMin, 0.1, 1)

			timeFormat = imguiEnum("time format", timeFormat, {
				'total', 'per frame avg', 'per call avg', 'percent', 'parent percent'
			})

			imgui.EndTable()
		end


		local size =  imgui.GetContentRegionAvail()
		if imgui.BeginListBox("##calltree", size) then
			if cprof.frameCount > 0 then
				if     timeFormat == 'total'          then imgui.Text(string.format("Profiler: %s", simpletimef(cprof.getProfTime())))
				elseif timeFormat == 'per frame avg'  then imgui.Text(string.format("Profiler: %s", simpletimef(cprof.getProfTime() / cprof.frameCount)))
				elseif timeFormat == 'per call avg'   then imgui.Text(string.format("Profiler (per frame): %s", simpletimef(cprof.getProfTime() / cprof.frameCount)))
				elseif timeFormat == 'percent'        then imgui.Text(string.format("Profiler (per frame): %s", simpletimef(cprof.getProfTime() / cprof.frameCount)))
				elseif timeFormat == 'parent percent' then imgui.Text(string.format("Profiler (per frame): %s", simpletimef(cprof.getProfTime() / cprof.frameCount)))
				else log("[cprof] error! 'timeFormat' is invalid!", "error") end
			else
				imgui.Text(string.format("Profiler: %s", simpletimef(0)))
			end
			imgui.Text(string.format("Frames: %i", cprof.frameCount))
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

		local newState = cs.name ~= currentState
		currentState = cs.name

		if startOnStateChange and newState then
			doReset = true
			doProfile = true
			if stopOnStateChange then
				startOnStateChange = false
			end
		elseif stopOnStateChange and newState then
			doProfile = false
		end

		if doReset then
			cprof.reset()
			cprof.frameCount = 0
		end
	end

	if doProfile then
		cprof.start()
		cprof.frameCount = cprof.frameCount + 1
	end
end

return cprof
