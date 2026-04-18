# meowdev

meowdev is a Linux input interface, similar to evdev, that exposes input devices like keyboards and mice to userspace through a simple file-based API.

## Device Files

Input devices are exposed as files under `/dev/input/`. Each file is named to indicate its device type:

- **Keyboards** — `/dev/input/kbd0`, `/dev/input/kbd1`, ...
- **Mice** — `/dev/input/mouse0`, `/dev/input/mouse1`, ...

## Opening a Device

Before you can read from a device, you need to grab exclusive control of it. Only one process can hold a grab on a device at a time.

1. Open the device file with `open()`, using `O_RDONLY` or `O_RDWR`. Pass `O_NONBLOCK` if you want non-blocking reads.
2. Call `ioctl(fd, MDEVGRABDEV, 1)` to acquire the grab.
3. When you're done, release it with `ioctl(fd, MDEVGRABDEV, 0)`.

## Reading Events

Once you have the grab, you can `read()` from the fd. Each read returns one or more `mdev_event` structs:

```c
__attribute__((packed)) struct mdev_event {
    struct timeval timestamp;
    unsigned short type;
    unsigned short code;
    unsigned int value;
};
```

### Fields

**`timestamp`** — When the event was received, sourced from the monotonic clock (`CLOCK_MONOTONIC`), i.e. time elapsed since boot.

**`type`** — The category of event. Only two types are currently defined; ignore anything else:

| Type       | Description                                                 |
| ---------- | ----------------------------------------------------------- |
| `MDEV_KEY` | A key or button changed state (keyboards and mouse buttons) |
| `MDEV_REL` | A relative axis changed (mouse movement)                    |

**`code`** — Which key or axis the event refers to. For example, `MDEV_KEY_H` for the H key, or `MDEV_AXIS_X` for horizontal mouse movement.

**`value`** — The new state or delta:

| Event type                | Value        | Meaning                                            |
| ------------------------- | ------------ | -------------------------------------------------- |
| `MDEV_KEY` (keyboard)     | `0`          | Key released                                       |
| `MDEV_KEY` (keyboard)     | `1`          | Key pressed                                        |
| `MDEV_KEY` (keyboard)     | `2`          | Key held (repeat)                                  |
| `MDEV_KEY` (mouse button) | `0`          | Button released                                    |
| `MDEV_KEY` (mouse button) | `1`          | Button pressed                                     |
| `MDEV_REL`                | signed delta | Cast `value` to `int` to get the relative movement |

## Non-Blocking I/O

If the fd was opened with `O_NONBLOCK`, reads will return `EAGAIN` immediately when no events are queued rather than blocking. This lets you integrate meowdev into an event loop using `poll()` or `select()` on the fd.
