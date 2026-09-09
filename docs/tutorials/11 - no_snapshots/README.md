# Tutorial 11. Snapshot policies

## What you're going to learn

In this tutorial you're going to learn how Testo controls hypervisor snapshots with the `snapshots` test attribute. Choosing the right policy can save a lot of disk space without disabling Testo's test cache.

## Introduction

Test caching plays a major role in Testo Framework. It lets Testo reuse the results of successfully completed tests while their cache remains valid. Hypervisor snapshots are a separate mechanism: they let Testo restore virtual machines and flash drives to an earlier test state without replaying every intermediate test.

Snapshots are useful, but they can consume a lot of disk space. If a test involves five virtual machines and two flash drives, keeping that test as a restoration point may mean keeping snapshots for all seven virtual entities.

The interpreter provides three snapshot policies:

1. `snapshots: "always"` — keep a hypervisor snapshot for the test.
2. `snapshots: "never"` — do not keep a hypervisor snapshot for the test.
3. `snapshots: "auto"` — let Testo keep temporary snapshots while they help execution and discard them when they are no longer needed.

The old boolean `no_snapshots` attribute is no longer accepted by the interpreter. This tutorial uses the current `snapshots` syntax throughout.

## Leaf tests and `snapshots: "never"`

Let's take a look at the tests hierarchy we've got to this point:

![test hierarchy](imgs/test_hierarchy.svg)

We have ten tests in total. Some states are useful restoration points for later tests, while others are leaves in the tree and will never have children restored from them.

For leaf tests such as `test_ping` and `exchange_files_with_flash`, keeping a persistent hypervisor snapshot usually provides no benefit. We can therefore use `snapshots: "never"`:

```testo
[
    snapshots: "never"
]
test test_ping: client_prepare, server_prepare {
    client exec bash "ping 192.168.1.2 -c5"
    server exec bash "ping 192.168.1.1 -c5"
}

[
    snapshots: "never"
]
test exchange_files_with_flash: client_prepare, server_prepare {
    client exec bash "echo \"Hello from client!\" > /tmp/copy_me_to_server.txt"
    copy_file_with_flash("client", "server", "exchange_flash", "/tmp/copy_me_to_server.txt", "/tmp/copy_me_to_server.txt")
    server exec bash "cat /tmp/copy_me_to_server.txt"
}
```

The `snapshots` attribute is part of the [test attributes](../../reference/Tests.md), so changing it changes the test checksum and invalidates that test's cache once. Run the script after making the change:

![](imgs/terminal1.svg)

The modified tests run again because their checksums changed. Now run them once more:

![](imgs/terminal2.svg)

They are still cached even though they no longer keep hypervisor snapshots:

![No snapshots](imgs/no_snapshots.png)

This illustrates an important distinction:

1. **Cache metadata** records enough information for Testo to decide whether a test result is still valid.
2. **Hypervisor snapshots** provide a restorable VM/flash state from which later tests can continue.

`snapshots: "never"` disables the second mechanism, not the first one.

> A test with `snapshots: "never"` can remain `UP-TO-DATE`. The policy does not mean that the test will run every time.

For leaf tests this is usually an easy disk-space saving because no child test needs to restore from their final state.

## `snapshots: "never"` in intermediate tests

The trade-off becomes visible when an intermediate test does not keep a snapshot. Mark `client_unplug_nat` this way:

```testo
[
    snapshots: "never"
]
test client_unplug_nat: client_install_guest_additions {
    client unplug_nic("${client_hostname}", "${client_login}", "nat", "1111")
}
```

Run that test:

![](imgs/terminal3.svg)

It can still be cached:

![](imgs/terminal4.svg)

Now run `client_prepare`, which depends on `client_unplug_nat`:

![](imgs/terminal5.svg)

The interesting part is that `client_unplug_nat` may appear both as up to date and as something that must be replayed. Its cache metadata is valid, but there is no hypervisor snapshot representing its final state.

To get the VM into the state required by `client_prepare`, Testo walks upward until it finds an earlier test that still has a restorable hypervisor snapshot. In this example that earlier anchor is `client_install_guest_additions`. Testo restores that state, then replays `client_unplug_nat`, and only then runs `client_prepare`.

The process can be visualized as follows:

![search](imgs/search_en.svg)

If more intermediate tests use `snapshots: "never"`, more of the path may have to be replayed.

Run all tests again:

![](imgs/terminal6.svg)

Leaf tests still run normally as long as useful anchor states such as `client_prepare` and `server_prepare` remain directly restorable.

## Choosing anchor tests

Using `snapshots: "never"` everywhere is usually a bad idea. Consider what happens if both `client_prepare` and `server_prepare` also stop keeping snapshots:

![](imgs/terminal7.svg)

The execution queue grows because Testo must restore older anchors and replay longer paths before each child can run. Saving more disk space can therefore cost much more execution time.

A useful rule of thumb is:

1. Leaf tests are good candidates for `snapshots: "never"` because no child needs their final state.
2. Intermediate tests can use `never` when replaying them is cheap and an earlier anchor is nearby.
3. Tests with several important children are usually better kept as restoration anchors.
4. Very expensive setup tests may deserve `snapshots: "always"` even if they are not restored frequently, because replaying them would be costly.
5. `snapshots: "auto"` is a good general-purpose choice when you want Testo to balance temporary restoration points against disk usage automatically.

For the example hierarchy, keeping `client_prepare` and `server_prepare` as anchors while using `never` on inexpensive preparatory or leaf tests gives a practical balance between disk usage and execution time.

## Conclusions

The snapshot policy and the test cache are independent mechanisms. `snapshots: "never"` saves disk space but may force Testo to replay intermediate tests when a descendant needs to run. `snapshots: "always"` preserves a direct restoration point, and `snapshots: "auto"` lets Testo decide when a temporary snapshot is worth keeping.

Before choosing a policy, think about how often a state will be restored, how expensive the test is to replay, and how much disk space its snapshots consume.

The tutorial directory keeps its historical `no_snapshots` name for compatibility with existing links; the supported Testo language syntax is the `snapshots` attribute shown above.
