-- Project rust-analyzer settings for Neovim (needs `vim.o.exrc = true`).
-- firmware/.cargo/config.toml already picks the RISC-V target; checking all targets would also
-- build the firmware's test harness, which doesn't exist without std.
vim.lsp.config("rust-analyzer", {
    settings = {
        ["rust-analyzer"] = {
            check = { allTargets = false },
        },
    },
})
