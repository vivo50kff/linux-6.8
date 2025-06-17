FROM ubuntu:22.04
RUN apt-get update && apt-get install -y build-essential
COPY test_yat_casched /usr/bin/
COPY arch/x86/boot/bzImage /boot/
ENTRYPOINT ["/usr/bin/test_yat_casched"]
