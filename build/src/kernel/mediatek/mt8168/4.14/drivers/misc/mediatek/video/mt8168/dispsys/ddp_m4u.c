/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "ddp_m4u.h"
#include "ddp_dump.h"
#include "ddp_hal.h"
#include "ddp_reg.h"
#include "ddp_log.h"
#include "disp_helper.h"

#ifdef CONFIG_MTK_IOMMU
/*#include "mach/mt_iommu.h"*/
#elif defined(CONFIG_MTK_M4U)
#include "m4u.h"
#endif


/* display m4u port / display module mapping table
 * -- by chip
 */
#define DISP_PAGE_MASK 0xfffL
static struct module_to_m4u_port_t module_to_m4u_port_mapping[] = {
	{DISP_MODULE_OVL0, 0, DISP_M4U_PORT_DISP_OVL0},
	{DISP_MODULE_OVL0_2L, 0, DISP_M4U_PORT_DISP_OVL0_2L},
	{DISP_MODULE_RDMA0, 0, DISP_M4U_PORT_DISP_RDMA0},
	{DISP_MODULE_RDMA1, 0, DISP_M4U_PORT_DISP_RDMA1},
	{DISP_MODULE_WDMA0, 0, DISP_M4U_PORT_DISP_WDMA0},
};

int module_to_m4u_port(enum DISP_MODULE_ENUM module)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(module_to_m4u_port_mapping); i++)
		if (module_to_m4u_port_mapping[i].module == module)
			return module_to_m4u_port_mapping[i].port;

	DDPERR("%s, get m4u port fail(module=%d)\n", __func__, module);
	return -1;
}

int module_to_m4u_larb(enum DISP_MODULE_ENUM module)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(module_to_m4u_port_mapping); i++)
		if (module_to_m4u_port_mapping[i].module == module)
			return module_to_m4u_port_mapping[i].larb;

	DDPERR("module_to_m4u_port, get m4u larb fail(module=%d)\n", module);
	return -1;
}

enum DISP_MODULE_ENUM m4u_port_to_module(int port)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(module_to_m4u_port_mapping); i++)
		if (module_to_m4u_port_mapping[i].port == port)
			return module_to_m4u_port_mapping[i].module;

	DDPERR("%s, unknown port=%d\n", __func__, port);
	return DISP_MODULE_UNKNOWN;
}

void disp_m4u_init(void)
{
	unsigned int i;

	if (disp_helper_get_option(DISP_OPT_USE_M4U)) {
		/* init M4U callback */
		DDPMSG("register m4u callback\n");
		for (i = 0; i < ARRAY_SIZE(module_to_m4u_port_mapping); i++) {
#ifdef CONFIG_MTK_IOMMU
			/*mtk_iommu_register_fault_callback(
			 *	module_to_m4u_port_mapping[i].port,
			 *	(mtk_iommu_fault_callback_t)disp_m4u_callback,
			 *	0);
			 */
#elif defined(CONFIG_MTK_M4U)
			m4u_register_fault_callback(
				module_to_m4u_port_mapping[i].port,
				(m4u_fault_callback_t *)disp_m4u_callback, 0);
#endif
		}
	} else {
		/* disable m4u port, used for m4u not ready */
		DDPMSG("m4u not enable, disable m4u port\n");
		for (i = 0; i < 4; i++)
			DISP_REG_SET_FIELD(0, REG_FLD_MMU_EN,
				DISP_REG_SMI_LARB0_NON_SEC_CON + i * 4, 0);
	}
}

int config_display_m4u_port(void)
{
	int ret = 0;
#ifdef CONFIG_MTK_IOMMU
	/* if you config to pa mode, please contact iommu owner */
	/*
	 * struct device *disp_larbdev = NULL;
	 * ret = mtk_smi_larb_get_ext(disp_larbdev);
	 */
#elif defined(CONFIG_MTK_M4U)
	struct M4U_PORT_STRUCT sPort;
	unsigned int i;
	enum DISP_MODULE_ENUM module;
	char *m4u_usage = disp_helper_get_option(DISP_OPT_USE_M4U) ?
		"virtual" : "physical";

	sPort.ePortID = M4U_PORT_NR;
	sPort.Virtuality = disp_helper_get_option(DISP_OPT_USE_M4U);
	sPort.Security = 0;
	sPort.Distance = 1;
	sPort.Direction = 0;

	for (i = 0; i < ARRAY_SIZE(module_to_m4u_port_mapping); i++) {
		sPort.ePortID = module_to_m4u_port_mapping[i].port;
		ret = m4u_config_port(&sPort);
		module = module_to_m4u_port_mapping[i].module;
		if (ret) {
			DISPERR("config M4U Port %s to %s FAIL(ret=%d)\n",
				ddp_get_module_name(module), m4u_usage, ret);
			return -1;
		}
	}
#endif
	return ret;
}

int disp_m4u_callback(int port, unsigned long mva, void *data)
{
	enum DISP_MODULE_ENUM module;

	DDPERR("fault call port=%d, mva=0x%lx, data=0x%p\n", port, mva, data);
	module = m4u_port_to_module(port);
	ddp_dump_analysis(module);
	ddp_dump_reg(module);

	return 0;
}

int disp_mva_map_kernel(enum DISP_MODULE_ENUM module, unsigned int mva,
	unsigned int size, unsigned long *map_va, unsigned int *map_size)
{
#ifdef CONFIG_MTK_IOMMU
	struct disp_iommu_device *disp_dev = disp_get_iommu_dev();

	if ((disp_dev != NULL) && (disp_dev->iommu_pdev != NULL))
		/*mtk_iommu_iova_to_va(&(disp_dev->iommu_pdev->dev),
		 *	mva, map_va, size);
		 */
		pr_info("%s need mtk_iommu_iova_to_va?\n", __func__);
	else
		pr_info("%s is null\n", __func__);
#elif defined(CONFIG_MTK_M4U)
	m4u_mva_map_kernel(mva, size, map_va, map_size);
#endif

	return 0;
}

int disp_mva_unmap_kernel(unsigned int mva, unsigned int size,
	unsigned long map_va)
{
#ifdef CONFIG_MTK_IOMMU
	vunmap((void *)(map_va & (~DISP_PAGE_MASK)));
#else
	vunmap((void *)(map_va & (~DISP_PAGE_MASK)));
#endif
	return 0;
}

struct ion_client *disp_ion_create(const char *name)
{
	struct ion_client *disp_ion_client = NULL;

#if defined(MTK_FB_ION_SUPPORT)
	if (g_ion_device)
		disp_ion_client = ion_client_create(g_ion_device, name);

	if (!disp_ion_client) {
		DDPERR("create ion client failed!\n");
		return NULL;
	}

	DDPDBG("create ion client 0x%p\n", disp_ion_client);
#endif
	return disp_ion_client;
}

struct ion_handle *disp_ion_alloc(struct ion_client *client,
	unsigned int heap_id_mask, size_t align, unsigned int size)
{
	struct ion_handle *disp_handle = NULL;

#if defined(MTK_FB_ION_SUPPORT)
	disp_handle = ion_alloc(client, size, align, heap_id_mask, 0);
	if (IS_ERR(disp_handle)) {
		DISPERR("%s error %p\n", __func__, disp_handle);
		return NULL;
	}

	DDPDBG("%s %p\n", __func__, disp_handle);
#endif
	return disp_handle;
}

int disp_ion_get_mva(struct ion_client *client, struct ion_handle *handle,
	unsigned long *mva, int port)
{
#if defined(MTK_FB_ION_SUPPORT)
	struct ion_mm_data mm_data;
	size_t mva_size;

	memset((void *)&mm_data, 0, sizeof(struct ion_mm_data));
	mm_data.config_buffer_param.module_id = port;
	mm_data.config_buffer_param.kernel_handle = handle;
	mm_data.mm_cmd = ION_MM_CONFIG_BUFFER;
	if (ion_kernel_ioctl(client, ION_CMD_MULTIMEDIA,
		(unsigned long)&mm_data) < 0) {

		DISPERR("%s: config buffer failed.%p -%p\n",
			__func__, client, handle);
		ion_free(client, handle);
		return -1;
	}

	ion_phys(client, handle,
		(ion_phys_addr_t *)mva, &mva_size);

	if (*mva == 0)
		DDPERR("alloc mmu addr hnd=0x%p,mva=0x%08lx\n",
			handle, *mva);
#endif
	return 0;
}

struct ion_handle *disp_ion_import_handle(struct ion_client *client, int fd)
{
	struct ion_handle *handle = NULL;
#if defined(MTK_FB_ION_SUPPORT)
	struct ion_mm_data mm_data;

	/* If no need Ion support, do nothing! */
	if (fd <= 0) {
		DDPERR("NO NEED ion support, fd %d\n", fd);
		return handle;
	}

	if (!client) {
		DDPERR("invalid ion client!\n");
		return handle;
	}

	handle = ion_import_dma_buf_fd(client, fd);
	if (IS_ERR(handle)) {
		DDPERR("import ion handle failed!\n");
		return NULL;
	}
	mm_data.mm_cmd = ION_MM_CONFIG_BUFFER;
	mm_data.config_buffer_param.kernel_handle = handle;
	mm_data.config_buffer_param.module_id = 0;
	mm_data.config_buffer_param.security = 0;
	mm_data.config_buffer_param.coherent = 0;

	if (ion_kernel_ioctl(client, ION_CMD_MULTIMEDIA,
		(unsigned long)&mm_data))
		DDPERR("configure ion buffer failed!\n");

	DDPDBG("import ion handle fd=%d,hnd=0x%p\n", fd, handle);
#endif
	return handle;
}

void disp_ion_free_handle(struct ion_client *client, struct ion_handle *handle)
{
#if defined(MTK_FB_ION_SUPPORT)
	if (!client) {
		DDPERR("invalid ion client!\n");
		return;
	}
	if (!handle)
		return;

	ion_free(client, handle);

	DDPDBG("free ion handle 0x%p\n", handle);
#endif
}

void disp_ion_destroy(struct ion_client *client)
{
#if defined(MTK_FB_ION_SUPPORT)
	if (client && g_ion_device)
		ion_client_destroy(client);
#endif
}

void disp_ion_cache_flush(struct ion_client *client,
	struct ion_handle *handle, enum ION_CACHE_SYNC_TYPE sync_type)
{
#if defined(MTK_FB_ION_SUPPORT)
	struct ion_sys_data sys_data;

	if (!client || !handle)
		return;

	sys_data.sys_cmd = ION_SYS_CACHE_SYNC;
	sys_data.cache_sync_param.kernel_handle = handle;
	sys_data.cache_sync_param.sync_type = sync_type;

	if (ion_kernel_ioctl(client, ION_CMD_SYSTEM,
		(unsigned long)&sys_data))
		DDPERR("ion cache flush failed!\n");
#endif
}

#ifdef CONFIG_MTK_M4U
static struct sg_table table;

int disp_allocate_mva(struct m4u_client_t *client, enum DISP_MODULE_ENUM module,
	unsigned long va, struct sg_table *sg_table,
	unsigned int size, unsigned int prot,
	unsigned int flags, unsigned int *pMva)
{
	int port = module_to_m4u_port(module);

	if (port == -1)
		return 1; /* err */

	return m4u_alloc_mva(client, port, va, sg_table, size, prot,
		flags, pMva);
}
#endif

int disp_hal_allocate_framebuffer(phys_addr_t pa_start, phys_addr_t pa_end,
				  unsigned long *va, unsigned long *mva)
{
#ifdef CONFIG_MTK_M4U
	int ret = 0;

	*va = (unsigned long)ioremap_nocache(pa_start, pa_end - pa_start + 1);
	pr_info("disphal_allocate_fb, pa_start=0x%pa, pa_end=0x%pa, va=0x%lx\n",
		&pa_start, &pa_end, *va);

	if (disp_helper_get_option(DISP_OPT_USE_M4U)) {
		struct m4u_client_t *client;

		struct sg_table *sg_table = &table;

		sg_alloc_table(sg_table, 1, GFP_KERNEL);

		sg_dma_address(sg_table->sgl) = pa_start;
		sg_dma_len(sg_table->sgl) = (pa_end - pa_start + 1);
		client = m4u_create_client();
		if (IS_ERR_OR_NULL(client))
			DISPERR("create client fail!\n");

		*mva = pa_start & 0xffffffffULL;
		ret = m4u_alloc_mva(client, DISP_M4U_PORT_DISP_OVL0, 0,
			sg_table, (pa_end - pa_start + 1),
		    M4U_PROT_READ | M4U_PROT_WRITE, 0, (unsigned int *)mva);
		if (ret)
			DISPERR("m4u_alloc_mva returns fail: %d\n", ret);

		DISPINFO("[DISPHAL] FB MVA is 0x%lx PA is 0x%pa\n",
			*mva, &pa_start);
	} else {
		*mva = pa_start & 0xffffffffULL;
	}
#else
#ifdef FREE_FB_BUFFER
	*va = (unsigned long)__va(pa_start);
#else
	*va = (unsigned long)ioremap_nocache(pa_start, pa_end - pa_start + 1);
#endif
	*mva = pa_start & 0xffffffffULL;
	pr_info("disphal_allocate_fb, pa=%pa, va=0x%lx mva=0x%lx\n",
		&pa_start, *va, *mva);
#endif
	return 0;
}
