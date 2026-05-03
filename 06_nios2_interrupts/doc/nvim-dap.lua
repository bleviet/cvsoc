-- Nios II / cvsoc debug adapter configuration for nvim-dap.
--
-- Requires LazyVim extra:  lazyvim.plugins.extras.dap.core  (already enabled)
-- Mason package installed: cpptools  (already installed — provides OpenDebugAD7)
--
-- Usage:
--   1. Start the GDB server in a terminal:
--        cd ~/workspace/bleviet/cvsoc/06_nios2_interrupts/quartus
--        make gdb-server
--   2. Open main.c in Neovim, then press <leader>dc and pick the config below.

local repo = vim.fn.expand("~/workspace/bleviet/cvsoc")

return {
    -- 1. Ensure cpptools is installed and its adapter is auto-registered.
    {
        "jay-babu/mason-nvim-dap.nvim",
        optional = true,
        opts = {
            ensure_installed = { "cppdbg" },
            handlers = {}, -- auto-register adapters for all installed mason packages
        },
    },

    -- 2. Register the cppdbg adapter and add the Nios II debug configuration.
    --    LazyVim's extras.dap.core sets config = function() end (empty), so
    --    overriding it here only adds our setup without losing any LazyVim setup
    --    (keymaps live in the `keys` table and are unaffected by config overrides).
    {
        "mfussenegger/nvim-dap",
        optional = true,
        config = function()
            local dap = require("dap")

            -- Verbose logging — helps diagnose adapter startup failures.
            dap.set_log_level("DEBUG")

            -- Explicitly register cppdbg in case mason-nvim-dap auto-registration
            -- hasn't fired yet (e.g. first launch before handlers run).
            if not dap.adapters.cppdbg then
                dap.adapters.cppdbg = {
                    id      = "cppdbg",
                    type    = "executable",
                    command = vim.fn.stdpath("data")
                        .. "/mason/packages/cpptools/extension/debugAdapters/bin/OpenDebugAD7",
                }
            end

            -- Nios II bare-metal debug configuration.
            -- Connects to nios2-gdb-server on port 2345 via the Docker GDB wrapper.
            -- miDebuggerServerAddress tells cppdbg this is a remote session so it
            -- skips native-ELF validation and issues "target remote" itself.
            dap.configurations.c = dap.configurations.c or {}
            table.insert(dap.configurations.c, {
                name    = "Nios II: 06 — Interrupts",
                type    = "cppdbg",
                request = "launch",

                -- ELF with debug symbols built by: make app
                program = repo .. "/06_nios2_interrupts/software/app/nios2_interrupts.elf",
                cwd     = repo .. "/06_nios2_interrupts/software/app",

                -- nios2-elf-gdb runs inside the cvsoc/quartus:23.1 Docker image
                MIMode                   = "gdb",
                miDebuggerPath           = repo .. "/common/docker/nios2-elf-gdb-wrapper.sh",
                -- Tells cppdbg to issue "target remote" instead of a local exec-run
                miDebuggerServerAddress  = "localhost:2345",

                setupCommands = {
                    -- Flash ELF into Nios II on-chip RAM
                    { text = "load",             ignoreFailures = false },
                    -- Break when a button press triggers the ISR
                    { text = "break button_isr", ignoreFailures = false },
                },

                launchCompleteCommand = "exec-continue",
            })
        end,
    },
}
