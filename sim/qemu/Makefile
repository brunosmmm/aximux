QEMU ?= install/bin/qemu-system-arm

.PHONY: qemu guest test clean

qemu:
	./scripts/build-qemu.sh

guest:
	./scripts/build-guest.sh

test: $(QEMU)
	@test -f guest/out/kbuild/arch/arm/boot/zImage || $(MAKE) guest
	./scripts/run-test.sh

$(QEMU):
	$(MAKE) qemu

clean:
	rm -rf build install guest/out
	# leave upstream/ clone intact
