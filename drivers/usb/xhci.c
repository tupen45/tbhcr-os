#include "drivers/usb/xhci.h"
#include "drivers/usb/usb_host.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io.h"
#include "kmalloc.h"
#include "pci.h"

#define XHCI_MAX_SLOTS_LOCAL 32
#define XHCI_MAX_EP_ID       31
#define XHCI_CMD_RING_TRBS   256
#define XHCI_EVENT_RING_TRBS 256
#define XHCI_EP0_RING_TRBS   256
#define XHCI_BULK_RING_TRBS  4096

#define XHCI_TRB_TYPE_SHIFT 10
#define XHCI_TRB_TYPE(t)    ((uint32_t)(t) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_CYCLE      (1u << 0)
#define XHCI_TRB_IOC        (1u << 5)
#define XHCI_TRB_IDT        (1u << 6)
#define XHCI_TRB_TC         (1u << 1)
#define XHCI_TRB_DIR_IN     (1u << 16)
#define XHCI_TRB_ISP        (1u << 2)
#define XHCI_TRB_BSR        (1u << 9)

#define XHCI_TRB_NORMAL     1
#define XHCI_TRB_SETUP      2
#define XHCI_TRB_DATA       3
#define XHCI_TRB_STATUS     4
#define XHCI_TRB_LINK       6
#define XHCI_TRB_ENABLE_SLOT 9
#define XHCI_TRB_ADDR_DEV   11
#define XHCI_TRB_CONFIG_EP  12
#define XHCI_TRB_EVAL_CTX   13
#define XHCI_TRB_DISABLE_SLOT 10
#define XHCI_TRB_TRANSFER   32
#define XHCI_TRB_COMPLETION 33
#define XHCI_TRB_PORT_STATUS 34
#define XHCI_TRB_HC_EVENT   37

#define XHCI_TRB_LEN(v)         ((uint32_t)(v) & 0x1FFFFu)
#define XHCI_TRB_INTR_TARGET(v) (((uint32_t)(v) & 0x3FFu) << 22)
#define XHCI_TRB_TD_SIZE(v)     ((((uint32_t)(v) > 31u ? 31u : (uint32_t)(v)) & 0x1Fu) << 17)
#define XHCI_TRB_TX_TYPE(v)     ((uint32_t)(v) << 16)
#define XHCI_TRB_DATA_OUT       2
#define XHCI_TRB_DATA_IN        3

#define XHCI_CMD_RUN        (1u << 0)
#define XHCI_CMD_RESET      (1u << 1)
#define XHCI_CMD_EIE        (1u << 2)
#define XHCI_CMD_HSEIE      (1u << 3)

#define XHCI_STS_HALT       (1u << 0)
#define XHCI_STS_FATAL      (1u << 2)
#define XHCI_STS_EINT       (1u << 3)
#define XHCI_STS_PORT       (1u << 4)
#define XHCI_STS_CNR        (1u << 11)
#define XHCI_STS_HCE        (1u << 12)

#define XHCI_IMAN_IP        (1u << 0)
#define XHCI_IMAN_IE        (1u << 1)

#define XHCI_ERST_EHB       (1u << 3)

#define XHCI_HCC_64BYTE_CONTEXT (1u << 2)
#define XHCI_HCC_PPC            (1u << 3)

#define XHCI_CTX_SIZE(hcc)      (((hcc) & XHCI_HCC_64BYTE_CONTEXT) ? 64u : 32u)
#define XHCI_HCS_MAX_SLOTS(v)   (((v) >> 0) & 0xFFu)
#define XHCI_HCS_MAX_PORTS(v)   (((v) >> 24) & 0xFFu)
#define XHCI_HCS_MAX_SP_HI(v)   (((v) >> 21) & 0x1Fu)
#define XHCI_HCS_MAX_SP_LO(v)   (((v) >> 27) & 0x1Fu)
#define XHCI_HCS_MAX_SCRATCH(v) ((XHCI_HCS_MAX_SP_HI(v) << 5) | XHCI_HCS_MAX_SP_LO(v))
#define XHCI_HCC_EXT_CAPS(v)    (((v) >> 16) & 0xFFFFu)
#define XHCI_DBOFF_MASK         0xFFFFFFFCu
#define XHCI_RTSOFF_MASK        (~0x1Fu)
#define XHCI_HC_BIOS_OWNED      (1u << 16)
#define XHCI_HC_OS_OWNED        (1u << 24)
#define XHCI_EXT_CAPS_LEGACY    1
#define XHCI_EXT_CAPS_ID(v)     ((v) & 0xFFu)
#define XHCI_EXT_CAPS_NEXT(v)   (((v) >> 8) & 0xFFu)

#define XHCI_COMP_SUCCESS       1
#define XHCI_COMP_SHORT_PACKET  13

#define XHCI_SLOT_FLAG          (1u << 0)
#define XHCI_EP0_FLAG           (1u << 1)
#define XHCI_LAST_CTX(v)        ((uint32_t)(v) << 27)
#define XHCI_ROOT_HUB_PORT(v)   (((uint32_t)(v) & 0xFFu) << 16)
#define XHCI_EP_TYPE(v)         ((uint32_t)(v) << 3)
#define XHCI_ERROR_COUNT(v)     (((uint32_t)(v) & 0x3u) << 1)
#define XHCI_MAX_PACKET(v)      (((uint32_t)(v) & 0xFFFFu) << 16)
#define XHCI_TR_DEQ_CYCLE       (1ull << 0)
#define XHCI_SLOT_ID_FOR_TRB(v) (((uint32_t)(v) & 0xFFu) << 24)
#define XHCI_EP_ID_FOR_TRB(v)   (((uint32_t)(v) & 0x1Fu) << 16)

#define XHCI_EP_TYPE_BULK_OUT   2
#define XHCI_EP_TYPE_CTRL       4
#define XHCI_EP_TYPE_BULK_IN    6

typedef struct {
    volatile uint32_t hc_capbase;
    volatile uint32_t hcs_params1;
    volatile uint32_t hcs_params2;
    volatile uint32_t hcs_params3;
    volatile uint32_t hcc_params1;
    volatile uint32_t db_off;
    volatile uint32_t run_regs_off;
    volatile uint32_t hcc_params2;
} xhci_cap_regs_t;

typedef struct {
    volatile uint32_t portsc;
    volatile uint32_t portpmsc;
    volatile uint32_t portli;
    volatile uint32_t porthlpmc;
} xhci_port_regs_t;

typedef struct {
    volatile uint32_t command;
    volatile uint32_t status;
    volatile uint32_t page_size;
    volatile uint32_t reserved1;
    volatile uint32_t reserved2;
    volatile uint32_t dev_notification;
    volatile uint32_t cmd_ring_lo;
    volatile uint32_t cmd_ring_hi;
    volatile uint32_t reserved3[4];
    volatile uint32_t dcbaa_lo;
    volatile uint32_t dcbaa_hi;
    volatile uint32_t config_reg;
    volatile uint32_t reserved4[241];
    xhci_port_regs_t port_regs[0];
} xhci_op_regs_t;

typedef struct {
    volatile uint32_t iman;
    volatile uint32_t imod;
    volatile uint32_t erst_size;
    volatile uint32_t reserved;
    volatile uint32_t erst_base_lo;
    volatile uint32_t erst_base_hi;
    volatile uint32_t erst_dequeue_lo;
    volatile uint32_t erst_dequeue_hi;
} xhci_intr_reg_t;

typedef struct {
    volatile uint32_t mfindex;
    volatile uint32_t reserved[7];
    xhci_intr_reg_t ir[1];
} xhci_run_regs_t;

typedef struct {
    uint32_t field[4];
} xhci_trb_t;

typedef struct {
    uint32_t dev_info;
    uint32_t dev_info2;
    uint32_t tt_info;
    uint32_t dev_state;
    uint32_t reserved[4];
} xhci_slot_ctx_t;

typedef struct {
    uint32_t ep_info;
    uint32_t ep_info2;
    uint32_t deq_lo;
    uint32_t deq_hi;
    uint32_t tx_info;
    uint32_t reserved[3];
} xhci_ep_ctx_t;

typedef struct {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
} xhci_input_control_ctx_t;

typedef struct {
    uint64_t ring_seg_base;
    uint32_t ring_seg_size;
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

typedef struct {
    xhci_trb_t *trbs;
    uintptr_t dma;
    int enqueue;
    int trb_count;
    int cycle;
} xhci_ring_t;

typedef struct {
    int configured;
    int ep_id;
    xhci_ring_t ring;
} xhci_endpoint_state_t;

typedef struct {
    int active;
    int slot_id;
    int port;
    int speed;
    uint16_t max_packet0;
    int max_ep_id;
    void *input_ctx;
    uintptr_t input_ctx_dma;
    void *device_ctx;
    uintptr_t device_ctx_dma;
    xhci_endpoint_state_t eps[XHCI_MAX_EP_ID + 1];
} xhci_device_t;

typedef struct {
    int active;
    int max_slots;
    int max_ports;
    uint32_t hcc_params1;
    uint32_t hcs_params2;
    uint32_t context_size;
    volatile uint8_t *mmio_base;
    volatile xhci_cap_regs_t *cap_regs;
    volatile xhci_op_regs_t *op_regs;
    volatile xhci_run_regs_t *run_regs;
    volatile uint32_t *doorbells;
    xhci_ring_t cmd_ring;
    xhci_trb_t *event_ring;
    uintptr_t event_ring_dma;
    int event_dequeue;
    int event_cycle;
    xhci_erst_entry_t *erst;
    uintptr_t erst_dma;
    uint64_t *dcbaa;
    uintptr_t dcbaa_dma;
    uint64_t *scratchpad_ptrs;
    xhci_device_t devices[XHCI_MAX_SLOTS_LOCAL + 1];
} xhci_controller_t;

static xhci_controller_t g_xhci;

static volatile void *map_mmio(uint32_t phys_addr) {
    return (volatile void *)(uintptr_t)phys_addr;
}

static uintptr_t virt_to_phys(void *ptr) {
    return (uintptr_t)ptr;
}

static inline uint32_t lower32(uint64_t v) {
    return (uint32_t)(v & 0xFFFFFFFFu);
}

static inline uint32_t upper32(uint64_t v) {
    return (uint32_t)((v >> 32) & 0xFFFFFFFFu);
}

static void xhci_write64(volatile uint32_t *reg_lo, uint64_t value) {
    reg_lo[0] = lower32(value);
    reg_lo[1] = upper32(value);
}

static uint64_t xhci_read64(volatile uint32_t *reg_lo) {
    uint64_t lo = reg_lo[0];
    uint64_t hi = reg_lo[1];
    return lo | (hi << 32);
}

static void xhci_delay(void) {
    for (volatile int i = 0; i < 200000; ++i) {
        __asm__ volatile("nop");
    }
}

static int xhci_wait_reg_clear(volatile uint32_t *reg, uint32_t mask, int loops) {
    for (int i = 0; i < loops; ++i) {
        if (((*reg) & mask) == 0) return 1;
        xhci_delay();
    }
    return 0;
}

static int xhci_wait_reg_set(volatile uint32_t *reg, uint32_t mask, int loops) {
    for (int i = 0; i < loops; ++i) {
        if (((*reg) & mask) == mask) return 1;
        xhci_delay();
    }
    return 0;
}

static void *xhci_ctx_at(void *base, uint32_t ctx_size, int index) {
    return (void *)((uint8_t *)base + (ctx_size * index));
}

static xhci_slot_ctx_t *xhci_input_slot_ctx(xhci_device_t *dev) {
    return (xhci_slot_ctx_t *)xhci_ctx_at(dev->input_ctx, g_xhci.context_size, 1);
}

static xhci_ep_ctx_t *xhci_input_ep_ctx(xhci_device_t *dev, int ep_id) {
    return (xhci_ep_ctx_t *)xhci_ctx_at(dev->input_ctx, g_xhci.context_size, ep_id + 1);
}

static xhci_input_control_ctx_t *xhci_input_ctrl_ctx(xhci_device_t *dev) {
    return (xhci_input_control_ctx_t *)xhci_ctx_at(dev->input_ctx, g_xhci.context_size, 0);
}

static int xhci_ep_id_from_addr(uint8_t ep_addr, int direction) {
    int ep_num = ep_addr & 0x7F;
    if (ep_num == 0) return 1;
    if (direction) return ep_num * 2 + 1;
    return ep_num * 2;
}

static int xhci_ep_type_for_bulk(int direction) {
    return direction ? XHCI_EP_TYPE_BULK_IN : XHCI_EP_TYPE_BULK_OUT;
}

static xhci_device_t *xhci_get_device(int slot_id) {
    if (slot_id <= 0 || slot_id > XHCI_MAX_SLOTS_LOCAL) return NULL;
    if (!g_xhci.devices[slot_id].active) return NULL;
    return &g_xhci.devices[slot_id];
}

static int xhci_init_ring(xhci_ring_t *ring, int trb_count) {
    if (!ring) return 0;
    memset(ring, 0, sizeof(*ring));
    ring->trbs = kmalloc_aligned((size_t)trb_count * sizeof(xhci_trb_t), 64);
    if (!ring->trbs) return 0;
    memset(ring->trbs, 0, (size_t)trb_count * sizeof(xhci_trb_t));
    ring->dma = virt_to_phys(ring->trbs);
    ring->enqueue = 0;
    ring->trb_count = trb_count;
    ring->cycle = 1;
    if (trb_count >= 2) {
        xhci_trb_t *link = &ring->trbs[trb_count - 1];
        link->field[0] = lower32((uint64_t)ring->dma);
        link->field[1] = upper32((uint64_t)ring->dma);
        link->field[2] = 0;
        link->field[3] = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE;
    }
    return 1;
}

static xhci_trb_t *xhci_ring_enqueue(xhci_ring_t *ring, uint64_t param, uint32_t status, uint32_t control) {
    if (!ring || !ring->trbs) return NULL;
    if (ring->enqueue >= ring->trb_count - 1) {
        xhci_trb_t *link = &ring->trbs[ring->trb_count - 1];
        link->field[3] = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC |
                         (ring->cycle ? XHCI_TRB_CYCLE : 0);
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }
    xhci_trb_t *trb = &ring->trbs[ring->enqueue++];
    trb->field[0] = lower32(param);
    trb->field[1] = upper32(param);
    trb->field[2] = status;
    trb->field[3] = control | (ring->cycle ? XHCI_TRB_CYCLE : 0);
    return trb;
}

static void xhci_ring_doorbell_host(void) {
    if (!g_xhci.doorbells) return;
    g_xhci.doorbells[0] = 0;
}

static void xhci_ring_doorbell_slot(int slot_id, int ep_id) {
    if (!g_xhci.doorbells) return;
    g_xhci.doorbells[slot_id] = (uint32_t)(ep_id & 0xFF);
}

static void xhci_update_erdp(void) {
    uintptr_t ptr = g_xhci.event_ring_dma + (uintptr_t)(g_xhci.event_dequeue * sizeof(xhci_trb_t));
    xhci_write64(&g_xhci.run_regs->ir[0].erst_dequeue_lo, (uint64_t)ptr | XHCI_ERST_EHB);
}

static int xhci_next_event(xhci_trb_t *event_out) {
    if (!g_xhci.event_ring || !event_out) return 0;
    if (g_xhci.event_dequeue >= XHCI_EVENT_RING_TRBS) {
        g_xhci.event_dequeue = 0;
        g_xhci.event_cycle ^= 1;
    }
    xhci_trb_t *trb = &g_xhci.event_ring[g_xhci.event_dequeue];
    if (((trb->field[3] & XHCI_TRB_CYCLE) ? 1 : 0) != g_xhci.event_cycle) {
        return 0;
    }
    memcpy(event_out, trb, sizeof(*event_out));
    g_xhci.event_dequeue++;
    if (g_xhci.event_dequeue >= XHCI_EVENT_RING_TRBS) {
        g_xhci.event_dequeue = 0;
        g_xhci.event_cycle ^= 1;
    }
    xhci_update_erdp();
    return 1;
}

static int xhci_comp_code(uint32_t status_field) {
    return (int)((status_field >> 24) & 0xFFu);
}

static int xhci_wait_for_event_type(int type, uint64_t match_param, xhci_trb_t *event_out) {
    xhci_trb_t event;
    for (int i = 0; i < 400000; ++i) {
        if (xhci_next_event(&event)) {
            int ev_type = (int)((event.field[3] >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu);
            if (ev_type == type) {
                uint64_t param = ((uint64_t)event.field[1] << 32) | event.field[0];
                if (!match_param || ((param & ~0xFULL) == (match_param & ~0xFULL))) {
                    if (event_out) *event_out = event;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int xhci_issue_command(uint64_t param, uint32_t status, uint32_t control, xhci_trb_t *event_out) {
    xhci_trb_t local_event;
    if (!event_out) event_out = &local_event;

    xhci_trb_t *cmd = xhci_ring_enqueue(&g_xhci.cmd_ring, param, status, control);
    if (!cmd) return 0;
    uint64_t cmd_dma = g_xhci.cmd_ring.dma + (uint64_t)((uintptr_t)(cmd - g_xhci.cmd_ring.trbs) * sizeof(xhci_trb_t));
    xhci_ring_doorbell_host();
    if (!xhci_wait_for_event_type(XHCI_TRB_COMPLETION, cmd_dma, event_out)) {
        printf("xhci: command 0x%x timed out\n", (control >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu);
        return 0;
    }
    if (xhci_comp_code(event_out->field[2]) != XHCI_COMP_SUCCESS) {
        printf("xhci: command 0x%x failed with completion %d\n",
               (control >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu,
               xhci_comp_code(event_out->field[2]));
        return 0;
    }
    return 1;
}

static int xhci_wait_for_transfer(uint64_t trb_dma, xhci_trb_t *event_out) {
    if (!xhci_wait_for_event_type(XHCI_TRB_TRANSFER, trb_dma, event_out)) {
        printf("xhci: transfer timed out\n");
        return 0;
    }
    int cc = xhci_comp_code(event_out->field[2]);
    if (cc != XHCI_COMP_SUCCESS && cc != XHCI_COMP_SHORT_PACKET) {
        printf("xhci: transfer failed with completion %d\n", cc);
        return 0;
    }
    return 1;
}

static void xhci_setup_slot_context(xhci_device_t *dev, int last_ctx) {
    xhci_slot_ctx_t *slot_ctx = xhci_input_slot_ctx(dev);
    memset(slot_ctx, 0, g_xhci.context_size);
    slot_ctx->dev_info = ((uint32_t)dev->speed << 20) | XHCI_LAST_CTX(last_ctx);
    slot_ctx->dev_info2 = XHCI_ROOT_HUB_PORT((uint32_t)(dev->port + 1));
}

static void xhci_setup_ep0_context(xhci_device_t *dev, uint16_t max_packet_size) {
    xhci_ep_ctx_t *ep0 = xhci_input_ep_ctx(dev, 1);
    memset(ep0, 0, g_xhci.context_size);
    ep0->ep_info2 = XHCI_ERROR_COUNT(3) |
                    XHCI_EP_TYPE(XHCI_EP_TYPE_CTRL) |
                    XHCI_MAX_PACKET(max_packet_size);
    ep0->deq_lo = lower32((uint64_t)dev->eps[1].ring.dma | XHCI_TR_DEQ_CYCLE);
    ep0->deq_hi = upper32((uint64_t)dev->eps[1].ring.dma | XHCI_TR_DEQ_CYCLE);
    ep0->tx_info = 8;
}

static void xhci_setup_endpoint_context(xhci_device_t *dev, int ep_id, int ep_type, uint16_t max_packet_size) {
    xhci_ep_ctx_t *ep = xhci_input_ep_ctx(dev, ep_id);
    memset(ep, 0, g_xhci.context_size);
    ep->ep_info2 = XHCI_ERROR_COUNT(3) |
                   XHCI_EP_TYPE(ep_type) |
                   XHCI_MAX_PACKET(max_packet_size);
    ep->deq_lo = lower32((uint64_t)dev->eps[ep_id].ring.dma | XHCI_TR_DEQ_CYCLE);
    ep->deq_hi = upper32((uint64_t)dev->eps[ep_id].ring.dma | XHCI_TR_DEQ_CYCLE);
    ep->tx_info = max_packet_size;
}

static int xhci_claim_bios(uint32_t hcc_params1) {
    uint32_t offset = XHCI_HCC_EXT_CAPS(hcc_params1) << 2;
    while (offset) {
        volatile uint32_t *reg = (volatile uint32_t *)(g_xhci.mmio_base + offset);
        uint32_t val = *reg;
        uint32_t next = XHCI_EXT_CAPS_NEXT(val);

        if (XHCI_EXT_CAPS_ID(val) == XHCI_EXT_CAPS_LEGACY) {
            if (val & XHCI_HC_BIOS_OWNED) {
                *reg = val | XHCI_HC_OS_OWNED;
                for (int i = 0; i < 100000; ++i) {
                    uint32_t now = *reg;
                    if ((now & XHCI_HC_BIOS_OWNED) == 0) break;
                    xhci_delay();
                }
            }
            return 1;
        }

        if (!next) break;
        offset += next << 2;
    }
    return 1;
}

static int xhci_prepare_scratchpads(void) {
    int count = (int)XHCI_HCS_MAX_SCRATCH(g_xhci.hcs_params2);
    if (count <= 0) return 1;

    g_xhci.scratchpad_ptrs = kmalloc_aligned((size_t)count * sizeof(uint64_t), 64);
    if (!g_xhci.scratchpad_ptrs) return 0;
    memset(g_xhci.scratchpad_ptrs, 0, (size_t)count * sizeof(uint64_t));

    for (int i = 0; i < count; ++i) {
        void *buf = kmalloc_aligned(4096, 4096);
        if (!buf) return 0;
        memset(buf, 0, 4096);
        g_xhci.scratchpad_ptrs[i] = virt_to_phys(buf);
    }
    g_xhci.dcbaa[0] = virt_to_phys(g_xhci.scratchpad_ptrs);
    return 1;
}

int xhci_init(uint8_t bus, uint8_t slot, uint8_t func, uint32_t bar) {
    memset(&g_xhci, 0, sizeof(g_xhci));

    g_xhci.mmio_base = map_mmio(bar);
    if (!g_xhci.mmio_base) return 0;

    g_xhci.cap_regs = (volatile xhci_cap_regs_t *)g_xhci.mmio_base;
    uint32_t hc_capbase = g_xhci.cap_regs->hc_capbase;
    uint32_t cap_length = hc_capbase & 0xFFu;
    g_xhci.hcc_params1 = g_xhci.cap_regs->hcc_params1;
    g_xhci.hcs_params2 = g_xhci.cap_regs->hcs_params2;

    if (!xhci_claim_bios(g_xhci.hcc_params1)) {
        printf("xhci: BIOS handoff failed\n");
    }

    g_xhci.context_size = XHCI_CTX_SIZE(g_xhci.hcc_params1);
    g_xhci.max_slots = (int)XHCI_HCS_MAX_SLOTS(g_xhci.cap_regs->hcs_params1);
    if (g_xhci.max_slots > XHCI_MAX_SLOTS_LOCAL) g_xhci.max_slots = XHCI_MAX_SLOTS_LOCAL;
    g_xhci.max_ports = (int)XHCI_HCS_MAX_PORTS(g_xhci.cap_regs->hcs_params1);

    g_xhci.op_regs = (volatile xhci_op_regs_t *)(g_xhci.mmio_base + cap_length);
    g_xhci.run_regs = (volatile xhci_run_regs_t *)(g_xhci.mmio_base + (g_xhci.cap_regs->run_regs_off & XHCI_RTSOFF_MASK));
    g_xhci.doorbells = (volatile uint32_t *)(g_xhci.mmio_base + (g_xhci.cap_regs->db_off & XHCI_DBOFF_MASK));

    printf("xhci: MMIO=%p caplen=%u slots=%d ports=%d ctx=%u\n",
           (void *)g_xhci.mmio_base, cap_length, g_xhci.max_slots, g_xhci.max_ports, g_xhci.context_size);

    uint32_t cmd = g_xhci.op_regs->command;
    cmd &= ~XHCI_CMD_RUN;
    g_xhci.op_regs->command = cmd;
    if (!xhci_wait_reg_set(&g_xhci.op_regs->status, XHCI_STS_HALT, 10000)) {
        printf("xhci: halt wait timed out\n");
    }

    g_xhci.op_regs->command |= XHCI_CMD_RESET;
    if (!xhci_wait_reg_clear(&g_xhci.op_regs->command, XHCI_CMD_RESET, 10000)) {
        printf("xhci: reset did not complete\n");
        return 0;
    }
    if (!xhci_wait_reg_clear(&g_xhci.op_regs->status, XHCI_STS_CNR, 10000)) {
        printf("xhci: controller not ready after reset\n");
        return 0;
    }

    if (!xhci_init_ring(&g_xhci.cmd_ring, XHCI_CMD_RING_TRBS)) {
        printf("xhci: command ring allocation failed\n");
        return 0;
    }

    g_xhci.event_ring = kmalloc_aligned((size_t)XHCI_EVENT_RING_TRBS * sizeof(xhci_trb_t), 64);
    g_xhci.erst = kmalloc_aligned(sizeof(xhci_erst_entry_t), 64);
    g_xhci.dcbaa = kmalloc_aligned((size_t)(g_xhci.max_slots + 1) * sizeof(uint64_t), 64);
    if (!g_xhci.event_ring || !g_xhci.erst || !g_xhci.dcbaa) {
        printf("xhci: event ring/DCBAA allocation failed\n");
        return 0;
    }
    memset(g_xhci.event_ring, 0, (size_t)XHCI_EVENT_RING_TRBS * sizeof(xhci_trb_t));
    memset(g_xhci.erst, 0, sizeof(xhci_erst_entry_t));
    memset(g_xhci.dcbaa, 0, (size_t)(g_xhci.max_slots + 1) * sizeof(uint64_t));
    g_xhci.event_ring_dma = virt_to_phys(g_xhci.event_ring);
    g_xhci.erst_dma = virt_to_phys(g_xhci.erst);
    g_xhci.dcbaa_dma = virt_to_phys(g_xhci.dcbaa);
    g_xhci.event_cycle = 1;

    g_xhci.erst[0].ring_seg_base = g_xhci.event_ring_dma;
    g_xhci.erst[0].ring_seg_size = XHCI_EVENT_RING_TRBS;

    xhci_write64(&g_xhci.op_regs->cmd_ring_lo, (uint64_t)g_xhci.cmd_ring.dma | 1u);
    xhci_write64(&g_xhci.op_regs->dcbaa_lo, g_xhci.dcbaa_dma);

    g_xhci.run_regs->ir[0].iman = XHCI_IMAN_IP;
    g_xhci.run_regs->ir[0].imod = 0;
    g_xhci.run_regs->ir[0].erst_size = 1;
    xhci_write64(&g_xhci.run_regs->ir[0].erst_base_lo, g_xhci.erst_dma);
    xhci_write64(&g_xhci.run_regs->ir[0].erst_dequeue_lo, (uint64_t)g_xhci.event_ring_dma | XHCI_ERST_EHB);

    if (!xhci_prepare_scratchpads()) {
        printf("xhci: scratchpad allocation failed\n");
        return 0;
    }

    g_xhci.op_regs->config_reg = (uint32_t)g_xhci.max_slots;

    cmd = g_xhci.op_regs->command;
    cmd |= XHCI_CMD_RUN | XHCI_CMD_EIE | XHCI_CMD_HSEIE;
    g_xhci.op_regs->command = cmd;
    if (!xhci_wait_reg_clear(&g_xhci.op_regs->status, XHCI_STS_HALT, 10000)) {
        printf("xhci: run wait timed out\n");
        return 0;
    }

    g_xhci.run_regs->ir[0].iman = XHCI_IMAN_IE | XHCI_IMAN_IP;

    if (g_xhci.hcc_params1 & XHCI_HCC_PPC) {
        for (int i = 0; i < g_xhci.max_ports; ++i) {
            uint32_t portsc = g_xhci.op_regs->port_regs[i].portsc;
            g_xhci.op_regs->port_regs[i].portsc = (portsc & ~XHCI_PORT_CHANGE_MASK) | XHCI_PORT_POWER;
        }
    }

    g_xhci.active = 1;
    return 1;
}

void xhci_poll(void) {
    if (!g_xhci.active) return;
    if (g_xhci.op_regs->status & (XHCI_STS_EINT | XHCI_STS_PORT)) {
        g_xhci.op_regs->status = XHCI_STS_EINT | XHCI_STS_PORT;
    }
    if (g_xhci.run_regs->ir[0].iman & XHCI_IMAN_IP) {
        g_xhci.run_regs->ir[0].iman = XHCI_IMAN_IE | XHCI_IMAN_IP;
    }
}

int xhci_is_active(void) {
    return g_xhci.active;
}

int xhci_get_port_count(void) {
    return g_xhci.max_ports;
}

uint32_t xhci_get_portsc(int port) {
    if (!g_xhci.active || port < 0 || port >= g_xhci.max_ports) return 0;
    return g_xhci.op_regs->port_regs[port].portsc;
}

int xhci_get_connected_port_count(void) {
    if (!g_xhci.active) return 0;
    int count = 0;
    for (int i = 0; i < g_xhci.max_ports; ++i) {
        if (g_xhci.op_regs->port_regs[i].portsc & XHCI_PORT_CONNECT) {
            count++;
        }
    }
    return count;
}

uint32_t xhci_get_first_connected_portsc(void) {
    if (!g_xhci.active) return 0;
    for (int i = 0; i < g_xhci.max_ports; ++i) {
        uint32_t portsc = g_xhci.op_regs->port_regs[i].portsc;
        if (portsc & XHCI_PORT_CONNECT) return portsc;
    }
    return 0;
}

void xhci_ack_port_changes(int port) {
    if (!g_xhci.active || port < 0 || port >= g_xhci.max_ports) return;
    uint32_t portsc = g_xhci.op_regs->port_regs[port].portsc;
    g_xhci.op_regs->port_regs[port].portsc = portsc | XHCI_PORT_CHANGE_MASK;
}

int xhci_port_speed(int port) {
    uint32_t portsc = xhci_get_portsc(port);
    return (int)((portsc & XHCI_PORT_SPEED_MASK) >> 10);
}

int xhci_reset_port(int port) {
    if (!g_xhci.active || port < 0 || port >= g_xhci.max_ports) return 0;

    uint32_t portsc = g_xhci.op_regs->port_regs[port].portsc;
    if (!(portsc & XHCI_PORT_CONNECT)) return 0;

    uint32_t write_val = (portsc & ~XHCI_PORT_CHANGE_MASK) | XHCI_PORT_RESET;
    if (g_xhci.hcc_params1 & XHCI_HCC_PPC) {
        write_val |= XHCI_PORT_POWER;
    }
    g_xhci.op_regs->port_regs[port].portsc = write_val;

    for (int i = 0; i < 10000; ++i) {
        uint32_t now = g_xhci.op_regs->port_regs[port].portsc;
        if ((now & XHCI_PORT_RESET) == 0 && (now & XHCI_PORT_CONNECT)) {
            g_xhci.op_regs->port_regs[port].portsc = now | XHCI_PORT_CHANGE_MASK;
            return 1;
        }
        xhci_delay();
    }

    printf("xhci: port %d reset timeout\n", port);
    return 0;
}

static int xhci_enable_slot(int *slot_id_out) {
    xhci_trb_t event;
    if (!xhci_issue_command(0, 0, XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT), &event)) {
        return 0;
    }
    int slot_id = (int)((event.field[3] >> 24) & 0xFFu);
    if (slot_id <= 0 || slot_id > XHCI_MAX_SLOTS_LOCAL) {
        printf("xhci: invalid slot id %d\n", slot_id);
        return 0;
    }
    if (slot_id_out) *slot_id_out = slot_id;
    return 1;
}

int xhci_address_device(int port, uint8_t max_packet_size0, int *slot_id_out) {
    if (!g_xhci.active) return 0;
    if (port < 0 || port >= g_xhci.max_ports) return 0;

    int slot_id = 0;
    if (!xhci_enable_slot(&slot_id)) return 0;

    xhci_device_t *dev = &g_xhci.devices[slot_id];
    memset(dev, 0, sizeof(*dev));
    dev->slot_id = slot_id;
    dev->port = port;
    dev->speed = xhci_port_speed(port);
    dev->max_packet0 = max_packet_size0;

    dev->input_ctx = kmalloc_aligned((size_t)g_xhci.context_size * 33u, 64);
    dev->device_ctx = kmalloc_aligned((size_t)g_xhci.context_size * 32u, 64);
    if (!dev->input_ctx || !dev->device_ctx) {
        printf("xhci: context allocation failed for slot %d\n", slot_id);
        return 0;
    }
    memset(dev->input_ctx, 0, (size_t)g_xhci.context_size * 33u);
    memset(dev->device_ctx, 0, (size_t)g_xhci.context_size * 32u);
    dev->input_ctx_dma = virt_to_phys(dev->input_ctx);
    dev->device_ctx_dma = virt_to_phys(dev->device_ctx);
    g_xhci.dcbaa[slot_id] = dev->device_ctx_dma;

    if (!xhci_init_ring(&dev->eps[1].ring, XHCI_EP0_RING_TRBS)) {
        printf("xhci: EP0 ring allocation failed\n");
        return 0;
    }
    dev->eps[1].configured = 1;
    dev->eps[1].ep_id = 1;
    dev->max_ep_id = 1;

    xhci_input_control_ctx_t *ctrl = xhci_input_ctrl_ctx(dev);
    memset(ctrl, 0, g_xhci.context_size);
    ctrl->add_flags = XHCI_SLOT_FLAG | XHCI_EP0_FLAG;
    xhci_setup_slot_context(dev, 1);
    xhci_setup_ep0_context(dev, max_packet_size0);

    if (!xhci_issue_command(dev->input_ctx_dma, 0,
                            XHCI_TRB_TYPE(XHCI_TRB_ADDR_DEV) |
                            XHCI_TRB_BSR |
                            XHCI_SLOT_ID_FOR_TRB(slot_id),
                            NULL)) {
        printf("xhci: default-state address command failed on port %d\n", port);
        return 0;
    }

    dev->active = 1;
    if (slot_id_out) *slot_id_out = slot_id;
    return 1;
}

int xhci_finish_address_device(int slot_id, uint16_t max_packet_size0) {
    xhci_device_t *dev = xhci_get_device(slot_id);
    if (!dev) return 0;

    memset(dev->input_ctx, 0, (size_t)g_xhci.context_size * 33u);
    xhci_input_ctrl_ctx(dev)->add_flags = XHCI_SLOT_FLAG | XHCI_EP0_FLAG;
    xhci_setup_slot_context(dev, 1);
    xhci_setup_ep0_context(dev, max_packet_size0);
    dev->max_packet0 = max_packet_size0;

    if (!xhci_issue_command(dev->input_ctx_dma, 0,
                            XHCI_TRB_TYPE(XHCI_TRB_ADDR_DEV) |
                            XHCI_SLOT_ID_FOR_TRB(slot_id),
                            NULL)) {
        printf("xhci: finish address device failed for slot %d\n", slot_id);
        return 0;
    }
    return 1;
}

int xhci_update_ep0(int slot_id, uint16_t max_packet_size) {
    xhci_device_t *dev = xhci_get_device(slot_id);
    if (!dev) return 0;

    memset(dev->input_ctx, 0, (size_t)g_xhci.context_size * 33u);
    xhci_input_ctrl_ctx(dev)->add_flags = XHCI_EP0_FLAG;
    xhci_setup_ep0_context(dev, max_packet_size);
    dev->max_packet0 = max_packet_size;

    if (!xhci_issue_command(dev->input_ctx_dma, 0,
                            XHCI_TRB_TYPE(XHCI_TRB_EVAL_CTX) | XHCI_SLOT_ID_FOR_TRB(slot_id),
                            NULL)) {
        printf("xhci: evaluate context failed for slot %d\n", slot_id);
        return 0;
    }
    return 1;
}

int xhci_control_transfer(int slot_id, usb_setup_packet_t *setup, void *data, int len) {
    xhci_device_t *dev = xhci_get_device(slot_id);
    if (!dev || !setup) return 0;

    xhci_ring_t *ring = &dev->eps[1].ring;
    if (!ring->trbs) {
        printf("xhci: EP0 ring full\n");
        return 0;
    }

    uint32_t setup_ctrl = XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT;
    if (len > 0) {
        setup_ctrl |= (setup->bmRequestType & 0x80) ?
            XHCI_TRB_TX_TYPE(XHCI_TRB_DATA_IN) :
            XHCI_TRB_TX_TYPE(XHCI_TRB_DATA_OUT);
    }
    uint64_t setup_param =
        (uint64_t)setup->bmRequestType |
        ((uint64_t)setup->bRequest << 8) |
        ((uint64_t)setup->wValue << 16) |
        ((uint64_t)setup->wIndex << 32) |
        ((uint64_t)setup->wLength << 48);
    if (!xhci_ring_enqueue(ring, setup_param,
                           XHCI_TRB_LEN(8) | XHCI_TRB_INTR_TARGET(0),
                           setup_ctrl)) {
        return 0;
    }

    if (len > 0) {
        uint64_t buf_dma = virt_to_phys(data);
        uint32_t data_ctrl = XHCI_TRB_TYPE(XHCI_TRB_DATA);
        if (setup->bmRequestType & 0x80) data_ctrl |= XHCI_TRB_DIR_IN | XHCI_TRB_ISP;
        if (!xhci_ring_enqueue(ring, buf_dma,
                               XHCI_TRB_LEN((uint32_t)len) | XHCI_TRB_TD_SIZE(0) | XHCI_TRB_INTR_TARGET(0),
                               data_ctrl)) {
            return 0;
        }
    }

    uint32_t status_ctrl = XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC;
    if (!(len > 0 && (setup->bmRequestType & 0x80))) {
        status_ctrl |= XHCI_TRB_DIR_IN;
    }
    xhci_trb_t *status_trb = xhci_ring_enqueue(ring, 0, XHCI_TRB_INTR_TARGET(0), status_ctrl);
    if (!status_trb) return 0;

    uint64_t status_dma = ring->dma + (uint64_t)((uintptr_t)(status_trb - ring->trbs) * sizeof(xhci_trb_t));
    xhci_ring_doorbell_slot(slot_id, 1);

    xhci_trb_t event;
    return xhci_wait_for_transfer(status_dma, &event);
}

int xhci_bulk_transfer_actual(int slot_id, uint8_t endpoint, int direction, void *data, int len, int *actual_len) {
    xhci_device_t *dev = xhci_get_device(slot_id);
    if (actual_len) *actual_len = 0;
    if (!dev || !data || len <= 0) return 0;

    int ep_id = xhci_ep_id_from_addr(endpoint, direction);
    if (ep_id <= 0 || ep_id > XHCI_MAX_EP_ID) return 0;
    if (!dev->eps[ep_id].configured) {
        printf("xhci: endpoint %d not configured on slot %d\n", ep_id, slot_id);
        return 0;
    }

    xhci_ring_t *ring = &dev->eps[ep_id].ring;
    if (!ring->trbs) {
        printf("xhci: endpoint ring %d full\n", ep_id);
        return 0;
    }

    uint32_t ctrl = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC;
    if (direction) ctrl |= XHCI_TRB_ISP;
    xhci_trb_t *trb = xhci_ring_enqueue(ring, virt_to_phys(data),
                                        XHCI_TRB_LEN((uint32_t)len) | XHCI_TRB_TD_SIZE(0) | XHCI_TRB_INTR_TARGET(0),
                                        ctrl);
    if (!trb) return 0;

    uint64_t trb_dma = ring->dma + (uint64_t)((uintptr_t)(trb - ring->trbs) * sizeof(xhci_trb_t));
    xhci_ring_doorbell_slot(slot_id, ep_id);

    xhci_trb_t event;
    if (!xhci_wait_for_transfer(trb_dma, &event)) return 0;
    if (actual_len) {
        int residue = (int)(event.field[2] & 0x00FFFFFFu);
        int actual = len - residue;
        if (actual < 0 || actual > len) actual = len;
        *actual_len = actual;
    }
    return 1;
}

int xhci_bulk_transfer(int slot_id, uint8_t endpoint, int direction, void *data, int len) {
    return xhci_bulk_transfer_actual(slot_id, endpoint, direction, data, len, NULL);
}

int xhci_configure_interface(int slot_id, const struct usb_interface_info *intf) {
    xhci_device_t *dev = xhci_get_device(slot_id);
    if (!dev || !intf) return 0;

    memset(dev->input_ctx, 0, (size_t)g_xhci.context_size * 33u);
    xhci_input_control_ctx_t *ctrl = xhci_input_ctrl_ctx(dev);
    ctrl->add_flags = XHCI_SLOT_FLAG;

    int max_ep_id = dev->max_ep_id;
    for (int i = 0; i < intf->num_endpoints; ++i) {
        uint8_t ep_addr = intf->endpoints[i].address;
        uint8_t ep_type = intf->endpoints[i].type;
        uint16_t mps = intf->endpoints[i].max_packet_size;
        if (ep_type != 2) continue;

        int direction = (ep_addr & 0x80) ? 1 : 0;
        int ep_id = xhci_ep_id_from_addr(ep_addr, direction);
        if (ep_id <= 0 || ep_id > XHCI_MAX_EP_ID) continue;

        int ring_trbs = (ep_id == 1) ? XHCI_EP0_RING_TRBS : XHCI_BULK_RING_TRBS;
        if (!dev->eps[ep_id].configured) {
            if (!xhci_init_ring(&dev->eps[ep_id].ring, ring_trbs)) {
                printf("xhci: endpoint ring allocation failed for ep %d\n", ep_id);
                return 0;
            }
            dev->eps[ep_id].ep_id = ep_id;
        }
        dev->eps[ep_id].configured = 1;
        ctrl->add_flags |= (1u << ep_id);
        xhci_setup_endpoint_context(dev, ep_id, xhci_ep_type_for_bulk(direction), mps);
        if (ep_id > max_ep_id) max_ep_id = ep_id;
    }

    xhci_setup_slot_context(dev, max_ep_id);
    if (!xhci_issue_command(dev->input_ctx_dma, 0,
                            XHCI_TRB_TYPE(XHCI_TRB_CONFIG_EP) | XHCI_SLOT_ID_FOR_TRB(slot_id),
                            NULL)) {
        printf("xhci: configure endpoint failed for slot %d\n", slot_id);
        return 0;
    }

    dev->max_ep_id = max_ep_id;
    return 1;
}

void xhci_handle_disconnect(int slot_id) {
    xhci_device_t *dev = xhci_get_device(slot_id);
    if (!dev) return;

    (void)xhci_issue_command(0, 0,
                             XHCI_TRB_TYPE(XHCI_TRB_DISABLE_SLOT) | XHCI_SLOT_ID_FOR_TRB(slot_id),
                             NULL);
    dev->active = 0;
    g_xhci.dcbaa[slot_id] = 0;
}
