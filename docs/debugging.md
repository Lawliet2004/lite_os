# Debugging

Phase 0/1 debugging support:

- COM1 serial output
- VGA text output
- panic message with source file and line
- CPU exception register dumps
- page-fault CR2 reporting
- controlled divide-error and page-fault smoke tests
- timer tick smoke check during boot
- QEMU serial log at `build/serial.log`

Useful commands:

```sh
make run
make debug-gdb
```

Future work:

- GDB helper script
- stack traces
- kernel log ring buffer
- `/dev/kmsg`
- syscall tracing
- allocation tracing
