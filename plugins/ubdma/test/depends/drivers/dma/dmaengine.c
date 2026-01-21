/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/dmaengine.h>

void dma_async_device_unregister(struct dma_device *device)
{
}

int dma_async_device_register(struct dma_device *device)
{
    return 0;
}

void dmaengine_desc_clear_reuse(struct dma_async_tx_descriptor *tx)
{
    tx->flags &= ~DMA_CTRL_REUSE;
}