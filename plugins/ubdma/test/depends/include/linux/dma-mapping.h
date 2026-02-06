/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#define DMA_MAPPING_ERROR (~(dma_addr_t)0)
#define dma_map_page(d, p, o, s, r) dma_map_page_attrs(d, p, o, s, r, 0)
#define dma_unmap_page(d, a, s, r) dma_unmap_page_attrs(d, a, s, r, 0)
static inline dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page, size_t offset, size_t size,
    enum dma_data_direction dir, unsigned long attrs)
{
    return DMA_MAPPING_ERROR;
}
static inline void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size, enum dma_data_direction dir,
    unsigned long attrs)
{
}

#endif /* _LINUX_DMA_MAPPING_H */
