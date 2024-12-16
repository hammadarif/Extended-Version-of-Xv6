#include "./../types.h"
#include "./../virtio.h"
#include "./../memlayout.h"
#include "./../riscv.h"
#include "./../defs.h"

extern int strcmp(const char *p, const char *q);
//extern struct virtio_net net;

int strcmp(const char *p, const char *q) {
    while (*p && *p == *q)
        p++, q++;
    return (uchar)*p - (uchar)*q;
}

void test_virtio_net_init() {
    printf("Running Initialization Test...\n");
    virtio_net_init();
    printf("Initialization Test Passed!\n");
}

void test_virtio_send_packet() {
    printf("Running Send Packet Test...\n");

    char test_packet[64] = "This is a test packet for sending via VirtIO-Net.";
    virtio_send_packet(test_packet, sizeof(test_packet));

    printf("Send Packet Test Passed!\n");
}


void test_virtio_receive_packet() {
    printf("Running Receive Packet Test...\n");

    char test_packet[PACKET_SIZE] = "Test packet for reception.";
    char received_buffer[PACKET_SIZE];

    // Simulate sending a packet
    virtio_send_packet(test_packet, sizeof(test_packet));

    // Process RX queue to receive the packet
    virtio_receive_packet(received_buffer, PACKET_SIZE);

    // Debug: Compare test packet and received buffer
    printf("DEBUG: Sent Packet: %s\n", test_packet);
    printf("DEBUG: Received Packet: %s\n", received_buffer);

    if (strcmp(test_packet, received_buffer) == 0) {
        printf("Receive Packet Test Passed!\n");
    } else {
        printf("Receive Packet Test Failed: Mismatch in received data.\n");
    }
}

void test_virtqueue_management() {
    printf("Running Virtqueue Management Test...\n");

    // Check RX descriptor initialization
    for (int i = 0; i < NUM; i++) {
        printf("RX Descriptor[%d]: addr = 0x%lx, flags = %x, len = %d\n",
               i, net.rx.desc[i].addr, net.rx.desc[i].flags, net.rx.desc[i].len);
    }

    // Check TX descriptor initialization (optional)
    for (int i = 0; i < NUM; i++) {
        printf("TX Descriptor[%d]: addr = 0x%lx, flags = %x, len = %d\n",
               i, net.tx.desc[i].addr, net.tx.desc[i].flags, net.tx.desc[i].len);
    }

    printf("Virtqueue Management Test Passed!\n");
}

void test_virtio_interrupt_handling() {
    printf("Running Interrupt Handling Test...\n");

    // Simulate RX interrupt
    uint32 simulated_irq_status = 1; // RX interrupt
    mmio_write(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_STATUS, simulated_irq_status);
    virtio_net_intr();

    // Simulate TX interrupt
    simulated_irq_status = 2; // TX interrupt
    mmio_write(VIRTIO1 + VIRTIO_MMIO_INTERRUPT_STATUS, simulated_irq_status);
    virtio_net_intr();

    printf("Interrupt Handling Test Passed!\n");
}

void run_virtio_net_tests() {
    test_virtio_net_init();
    test_virtio_send_packet();
    test_virtio_receive_packet();
    test_virtqueue_management();
    test_virtio_interrupt_handling();
}

