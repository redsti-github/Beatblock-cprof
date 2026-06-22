jit.off()

do -- load dll/so
	local path = love.filesystem.getSaveDirectory()
	local os = love.system.getOS()
	if os == "Windows" then
		path = path .. "\\Mods\\cprof\\?.dll"
	else
		path = path .. "/Mods/cprof/?.so"
	end
	local prevPackagePath = package.path
	local prevPackageCpath = package.cpath
	package.path = ""
	package.cpath = path
	cprof = require("cprof")
	package.path = prevPackagePath
	package.cpath = prevPackageCpath
end

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
local displayMinMs = 0.1
local displayMinPct = 5
local sortBy = 'by time'


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

local function getTime(info, parentInfo, rootInfo)
	if     timeFormat == 'total'          then return info.t
	elseif timeFormat == 'per frame avg'  then return info.t / cprof.frameCount
	elseif timeFormat == 'per call avg'   then return info.t / info.c
	elseif timeFormat == 'percent'        then return info.t / rootInfo.t
	elseif timeFormat == 'parent percent' then return info.t / parentInfo.t
	else
		log("[cprof] error! 'timeFormat' is invalid!", "error")
		return info.t
	end
end

local function getCallcount(info, parentInfo, rootInfo)
	if     timeFormat == 'total'          then return info.c
	elseif timeFormat == 'per frame avg'  then return info.c / cprof.frameCount
	elseif timeFormat == 'per call avg'   then return info.c / parentInfo.c
	elseif timeFormat == 'percent'        then return info.c
	elseif timeFormat == 'parent percent' then return info.c
	else
		log("[cprof] error! 'timeFormat' is invalid!", "error")
		return info.c
	end
end

local function timeAsPercent()
	if     timeFormat == 'total'          then return false
	elseif timeFormat == 'per frame avg'  then return false
	elseif timeFormat == 'per call avg'   then return false
	elseif timeFormat == 'percent'        then return true
	elseif timeFormat == 'parent percent' then return true
	else return false
	end
end

local function displayFormatTime(time)
	if timeAsPercent() then
		return string.format("%4.1f %% ", 100 * time)
	else
		return simpletimef(time)
	end
end

local function displayFormat(time, callcount, func)
	if     timeFormat == 'total'          then return string.format("%s [%i]  %s", displayFormatTime(time), callcount, finfo(func))
	elseif timeFormat == 'per frame avg'  then return string.format("%s [%.2f]  %s", displayFormatTime(time), callcount, finfo(func))
	elseif timeFormat == 'per call avg'   then return string.format("%s [%.2f]  %s", displayFormatTime(time), callcount, finfo(func))
	elseif timeFormat == 'percent'        then return string.format("%s [%i]  %s", displayFormatTime(time), callcount, finfo(func))
	elseif timeFormat == 'parent percent' then return string.format("%s [%i]  %s", displayFormatTime(time), callcount, finfo(func))
	else
		log("[cprof] error! 'timeFormat' is invalid!", "error")
		return "<error> whoops! </error>"
	end
end

local function shouldDisplay(time)
	if timeAsPercent() then
		return time >= displayMinPct/100
	else
		return time >= displayMinMs*1000 -- time is in microsecs
	end
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

local function drawProfTable(info, infoParent, infoRoot)
	local selfTime = info.t
	local func = info.f
	local accurate = true

	infoParent = infoParent or info
	infoRoot = infoRoot or info

	local time = getTime(info, infoParent, infoRoot)
	local callcount = getCallcount(info, infoParent, infoRoot)

	local subcalls = {}
	for key,child in pairs(info) do
		if key == "t" then goto continue end
		if key == "c" then goto continue end
		if key == "p" then goto continue end
		if key == "f" then goto continue end
		if key == "i" then accurate = false goto continue end

		selfTime = selfTime - child.t

		local time = getTime(child, info, infoRoot)
		local callcount = getCallcount(child, info, infoRoot)

		if shouldDisplay(time) then
			table.insert(subcalls, {next=child, time=time, callcount=callcount})
		end

		::continue::
	end

	-- sort by time taken
	if sortBy == 'by time' then
		table.sort(subcalls, function(a, b) return a.time > b.time end)
	elseif sortBy == 'by callcount' then
		table.sort(subcalls, function(a, b) return a.callcount > b.callcount end)
	end

	selfTime = getTime({t=selfTime, c=info.c}, info, infoRoot) -- this is evil, and i know it... but it works so no hate, mkay?

	-- print
	local text = displayFormat(time, callcount, func)
	if #subcalls == 0 then
		imgui.BulletText(text)
		if not accurate then accuracyNote() end
	elseif imgui.TreeNode_StrStr(tostring(func), "%s", text) then
		if not accurate then accuracyNote() end

		if shouldDisplay(selfTime) then
			imgui.BulletText(string.format("%s  (self)", displayFormatTime(selfTime)))
		end

		for _,v in ipairs(subcalls) do
			drawProfTable(v.next, info, infoRoot)
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
			timeFormat = imguiEnum("time format", timeFormat, {
				'total', 'per frame avg', 'per call avg', 'percent', 'parent percent'
			})

			if timeAsPercent() then
				displayMinPct = helpers.InputFloat("min display %", displayMinPct, 5, 20)
			else
				displayMinMs = helpers.InputFloat("min display ms", displayMinMs, 0.1, 1)
			end

			sortBy = imguiEnum("sort", sortBy, {'by time', 'by callcount'})

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
