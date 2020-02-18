# SymlinkCallback

Write-up: https://windows-internals.com/symhooks-part-two/

This driver uses the option to set a dynamic target for a symbolic link and hooks the symlink of the C: volume.

It modifies the symlink object and replaces the LinkTarget string with a callback function which will be called whenever the symlink is accessed.

Then, it creates a device object and redirects the symlink target to the device object, intercepting all file open operations through its IRP_MJ_CREATE handler. This handler prints out the target file name, and then reparses the name back to the original C: volume target device object.

Created by @aionescu (https://github.com/ionescu007/) and @yarden_shafir
