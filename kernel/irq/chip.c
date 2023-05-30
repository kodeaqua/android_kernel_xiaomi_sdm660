/*
 * linux/kernel/irq/chip.c
 *
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the core interrupt handling code, for irq-chip
 * based architectures.
 *
 * Detailed information is available in Documentation/DocBook/genericirq
 */

#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/irqdomain.h>

#include <trace/events/irq.h>

#include "internals.h"

static irqreturn_t bad_chained_irq(int irq, void *dev_id)
{
	WARN_ONCE(1, "Chained irq %d should not call an action\n", irq);
	return IRQ_NONE;
}

struct irqaction chained_action = {
	.handler = bad_chained_irq,
	.name = "chained-irq",
};

int irq_set_chip(unsigned int irq, struct irq_chip *chip)
{
	struct irq_desc *desc = irq_get_desc(irq);

	if (!desc)
		return -EINVAL;

	if (!chip)
		chip = &no_irq_chip;

	desc->irq_data.chip = chip;

	irq_mark_irq(irq);
	return 0;
}
EXPORT_SYMBOL(irq_set_chip);

int irq_set_irq_type(unsigned int irq, unsigned int type)
{
	struct irq_desc *desc = irq_get_desc(irq);
	int ret = 0;

	if (!desc)
		return -EINVAL;

	type &= IRQ_TYPE_SENSE_MASK;
	ret = __irq_set_trigger(desc, type);
	return ret;
}
EXPORT_SYMBOL(irq_set_irq_type);

int irq_set_handler_data(unsigned int irq, void *data)
{
	struct irq_desc *desc = irq_get_desc(irq);

	if (!desc)
		return -EINVAL;
	desc->irq_common_data.handler_data = data;
	return 0;
}
EXPORT_SYMBOL(irq_set_handler_data);

int irq_set_msi_desc_off(unsigned int irq_base, unsigned int irq_offset, struct msi_desc *entry)
{
	struct irq_desc *desc = irq_get_desc(irq_base + irq_offset);

	if (!desc)
		return -EINVAL;
	desc->irq_common_data.msi_desc = entry;
	if (entry && !irq_offset)
		entry->irq = irq_base;
	return 0;
}

int irq_set_msi_desc(unsigned int irq, struct msi_desc *entry)
{
	return irq_set_msi_desc_off(irq, 0, entry);
}

int irq_set_chip_data(unsigned int irq, void *data)
{
	struct irq_desc *desc = irq_get_desc(irq);

	if (!desc)
		return -EINVAL;
	desc->irq_data.chip_data = data;
	return 0;
}
EXPORT_SYMBOL(irq_set_chip_data);

struct irq_data *irq_get_irq_data(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return desc ? &desc->irq_data : NULL;
}
EXPORT_SYMBOL_GPL(irq_get_irq_data);

static void irq_state_clr_disabled(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_DISABLED);
}

static void irq_state_set_disabled(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
}

static void irq_state_clr_masked(struct irq_desc *desc)
{
	irqd_clear(&desc->irq_data, IRQD_IRQ_MASKED);
}

static void irq_state_set_masked(struct irq_desc *desc)
{
	irqd_set(&desc->irq_data, IRQD_IRQ_MASKED);
}

int irq_startup(struct irq_desc *desc, bool resend)
{
	int ret = 0;

	irq_state_clr_disabled(desc);
	desc->depth = 0;

	irq_domain_activate_irq(&desc->irq_data);
	if (desc->irq_data.chip->irq_startup) {
		ret = desc->irq_data.chip->irq_startup(&desc->irq_data);
		irq_state_clr_masked(desc);
	}

	return ret;
}

void irq_shutdown(struct irq_desc *desc)
{
	irq_state_set_masked(desc);
	irq_domain_deactivate_irq(&desc->irq_data);
	if (desc->irq_data.chip->irq_shutdown)
		desc->irq_data.chip->irq_shutdown(&desc->irq_data);
}

void irq_disable(struct irq_desc *desc)
{
	unsigned long flags;

	local_irq_save(flags);
	irq_state_set_disabled(desc);
	if (desc->depth++ == 0 && desc->irq_data.chip->irq_disable)
		desc->irq_data.chip->irq_disable(&desc->irq_data);
	local_irq_restore(flags);
}

void irq_enable(struct irq_desc *desc)
{
	unsigned long flags;

	local_irq_save(flags);
	if (--desc->depth == 0 && desc->irq_data.chip->irq_enable)
		desc->irq_data.chip->irq_enable(&desc->irq_data);
	irq_state_clr_disabled(desc);
	local_irq_restore(flags);
}
