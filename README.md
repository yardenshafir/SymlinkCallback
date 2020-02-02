# SymlinkCallback

Write-up: https://windows-internals.com/dkom-now-with-symbolic-links/

This driver uses the option to set a dynamic target for a symbolic link and hooks the symlink of the C: volume.
It modifies the symlink object and replaces the LinkTarget string with a callback function which will be called whenever the symlink is accessed.

Created by @aionescu (https://github.com/ionescu007/) and @yarden_shafir
