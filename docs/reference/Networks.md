# Virtual network declaration

A virtual network declaration starts with the `network` keyword and looks like this:

```text
network <name> {
  <attr1>: <value1>
  <attr2>: <value1>
  <attr3>: <value1>
  ...
}
```

> The declaration itself does not mean the actual creation of the virtual network . The actual creation happens when the first virtual machine with a NIC attached to this netowrk is created.

> Virtual networks can also be defined inside macros. See [here](Macros.md#macros-with-declarations) for more information.

A virtual network declaration is similar to a [virtual machine](Machines.md) declaration, but has a different set of attributes:

**Mandatory network attributes**

- `mode` - Type: string. Network type. Possible values:
  - `nat` - The network is NAT'ing to the default route of the Host. If the Host has the Internet access (via the default gateway), this network mode will provide the Internet access to the VM as well.
  - `internal`  The network is isolated from the Host routes. This mode is used to create connections between various virtual machines in the Test Bench.

## Example

```testo
network example_network {
  mode: "nat"
}
```

> (Applicable only to QEMU) When a virtual network with the `nat` mode is created, the address range 192.168.156.0/24 is assigned to it. If this range is already occupied, the next "free" range is taken (192.168.157.0/24, 192.168.158.0/24 and so on). If all the ranges until 192.168.254.0/24 is already taken, an error will be generated.

## QEMU user mode

The interpreter uses `TESTO_QEMU_ADDRESS` as the QEMU/libvirt address override, with `qemu:///system` as the default. Without `--user`, that address is used for VM, storage, and network operations. With `testo run --user`, VM domains and Testo storage stay in the user's `qemu:///session` libvirt instance while virtual networks use the separate `TESTO_QEMU_ADDRESS` connection. An explicitly empty `TESTO_QEMU_ADDRESS` value is passed to libvirt unchanged. Session VMs attach to the host bridge created for the Testo network.

QEMU's bridge helper must therefore allow that bridge when the network backend creates a host bridge. If startup fails with `access denied by acl file`, configure a per-user bridge ACL, for example:

```sh
echo "allow all" | sudo tee /etc/qemu/${USER}.conf
echo "include /etc/qemu/${USER}.conf" | sudo tee --append /etc/qemu/bridge.conf
sudo chown root:${USER} /etc/qemu/${USER}.conf
sudo chmod 640 /etc/qemu/${USER}.conf
```

Testo reports these commands when it detects this specific bridge-helper error; it does not modify the host ACL automatically.
