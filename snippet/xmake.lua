-- Find all directories in the current list directory
local dirs = os.dirs("*")

-- Iterate over directories and check if xmake.lua and .buildme exist
for _, dir in ipairs (dirs) do
    local xmakeFile = path.join(dir, "xmake.lua")
    local buildmeFile = path.join(dir, ".buildme")
    
    if os.isdir(dir) and os.isfile(xmakeFile) and os.isfile(buildmeFile) then
        -- Add subdirectory as a sub-project in xmake
        set_targetdir ("$(builddir)/$(plat)/$(arch)/$(mode)/snippet/")

        -- 空的 on_install(function () end)，他们会被 xmake 编译和链接，但 xmake install 不会安装它们。
        on_install(function () end)
        apply_current_platform_target_config()

        includes (dir)
    
    end

end
