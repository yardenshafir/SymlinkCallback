# SymlinkCallback

Write-ups: https://windows-internals.com/dkom-now-with-symbolic-links/, https://windows-internals.com/symhooks-part-two/
           
This driver uses the option to set a dynamic target for a symbolic link and hooks the symlink of the C: volume.

It modifies the symlink object and replaces the LinkTarget string with a callback function which will be called whenever the symlink is accessed.

Then, it creates a device object and redirects the symlink target to the device object, adding a "\Foo" suffix in order to avoid direct volume open attempts (which cannot be reparsed). This allows it to intercept all file open operations on the C: volume through its IRP_MJ_CREATE handler. This handler then reparses the name back to the original C: volume target device object, removing the "\Foo" suffix that was added.

Created by @aionescu (https://github.com/ionescu007/) and @yarden_shafir
