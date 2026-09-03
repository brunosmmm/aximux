// SPDX-License-Identifier: GPL-2.0-only
/*
 * AXIMUX — AXI soft-IP pin controller (PL mux)
 *
 * Pinmux programs SRCSEL.SRC. Pinconf covers SHORT / DIREN / DIRCTL.
 * Pins/groups/functions come from DT (1-pin and multi-pin). Runtime remux uses
 * pinctrl states; lab freeform uses debugfs pinmux-select. Sysfs exposes a
 * read-only "srcsel" dump only (no writeable mux API).
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#define DRIVER_NAME		"aximux"

#define AXIMUX_REG(n)		((n) << 2)
#define AXIMUX_REG_MUXINFO	0x80

#define AXIMUX_SRC_MASK		GENMASK(3, 0)
#define AXIMUX_SHORT_BIT	BIT(5)
#define AXIMUX_DIREN_BIT	BIT(6)
#define AXIMUX_DIRCTL_BIT	BIT(7)

#define AXIMUX_MAX_PINS		32
#define AXIMUX_MAX_ALTS		15

#define AXIMUX_CFG_SHORT	(PIN_CONFIG_END + 1)
#define AXIMUX_CFG_DIR_SW	(PIN_CONFIG_END + 2)
#define AXIMUX_CFG_DIR_OUT	(PIN_CONFIG_END + 3)

struct aximux_pin {
	unsigned number;
	const char *name;
	unsigned nfuncs;
	const char **func_names;
};

struct aximux_group {
	const char *name;
	const unsigned *pins;
	unsigned npins;
};

struct aximux_function {
	const char *name;
	const char **groups;
	unsigned ngroups;
	unsigned **mux_vals;
	unsigned *mux_npins;
};

struct aximux {
	struct device *dev;
	void __iomem *regs;
	struct pinctrl_dev *pctl;

	unsigned npins;
	unsigned nalts;
	struct aximux_pin *pins;
	struct pinctrl_pin_desc *pindescs;

	unsigned ngroups;
	struct aximux_group *groups;

	unsigned nfunctions;
	struct aximux_function *functions;
};

static u32 aximux_read(struct aximux *amx, unsigned pin)
{
	return ioread32(amx->regs + AXIMUX_REG(pin));
}

static void aximux_write(struct aximux *amx, unsigned pin, u32 val)
{
	iowrite32(val, amx->regs + AXIMUX_REG(pin));
}

static void aximux_set_src(struct aximux *amx, unsigned pin, unsigned src)
{
	u32 val = aximux_read(amx, pin);

	val = (val & ~AXIMUX_SRC_MASK) | (src & AXIMUX_SRC_MASK);
	aximux_write(amx, pin, val);
}

static int aximux_find_pin_by_name(struct aximux *amx, const char *name)
{
	unsigned i;

	for (i = 0; i < amx->npins; i++) {
		if (!strcmp(amx->pins[i].name, name))
			return amx->pins[i].number;
	}
	return -EINVAL;
}

static int aximux_add_function(struct aximux *amx, const char *name,
			       const char *group, unsigned npins,
			       const unsigned *mux_vals)
{
	struct aximux_function *fn;
	unsigned *vals;
	const char **groups;
	unsigned **all_mux;
	unsigned *all_npins;
	unsigned f;

	vals = kmemdup(mux_vals, npins * sizeof(*vals), GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	for (f = 0; f < amx->nfunctions; f++) {
		if (!strcmp(amx->functions[f].name, name))
			break;
	}

	if (f == amx->nfunctions) {
		fn = krealloc(amx->functions,
			      (amx->nfunctions + 1) * sizeof(*fn), GFP_KERNEL);
		if (!fn) {
			kfree(vals);
			return -ENOMEM;
		}
		amx->functions = fn;
		fn = &amx->functions[amx->nfunctions];
		memset(fn, 0, sizeof(*fn));
		fn->name = name;
		amx->nfunctions++;
	} else {
		fn = &amx->functions[f];
	}

	groups = krealloc(fn->groups, (fn->ngroups + 1) * sizeof(*groups),
			  GFP_KERNEL);
	all_mux = krealloc(fn->mux_vals, (fn->ngroups + 1) * sizeof(*all_mux),
			   GFP_KERNEL);
	all_npins = krealloc(fn->mux_npins,
			     (fn->ngroups + 1) * sizeof(*all_npins), GFP_KERNEL);
	if (!groups || !all_mux || !all_npins) {
		kfree(vals);
		return -ENOMEM;
	}

	fn->groups = groups;
	fn->mux_vals = all_mux;
	fn->mux_npins = all_npins;
	fn->groups[fn->ngroups] = group;
	fn->mux_vals[fn->ngroups] = vals;
	fn->mux_npins[fn->ngroups] = npins;
	fn->ngroups++;
	return 0;
}

static int aximux_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);

	return amx->ngroups;
}

static const char *aximux_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);

	return amx->groups[selector].name;
}

static int aximux_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
				 const unsigned **pins, unsigned *npins)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);

	*pins = amx->groups[selector].pins;
	*npins = amx->groups[selector].npins;
	return 0;
}

static void aximux_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned offset)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);
	u32 val = aximux_read(amx, offset);

	seq_printf(s, "src=%lu short=%u diren=%u dirctl=%u",
		   val & AXIMUX_SRC_MASK,
		   !!(val & AXIMUX_SHORT_BIT),
		   !!(val & AXIMUX_DIREN_BIT),
		   !!(val & AXIMUX_DIRCTL_BIT));
}

static const struct pinctrl_ops aximux_pinctrl_ops = {
	.get_groups_count = aximux_get_groups_count,
	.get_group_name = aximux_get_group_name,
	.get_group_pins = aximux_get_group_pins,
	.pin_dbg_show = aximux_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int aximux_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);

	return amx->nfunctions;
}

static const char *aximux_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned selector)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);

	return amx->functions[selector].name;
}

static int aximux_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned selector,
				      const char *const **groups,
				      unsigned *const ngroups)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);

	*groups = amx->functions[selector].groups;
	*ngroups = amx->functions[selector].ngroups;
	return 0;
}

static int aximux_set_mux(struct pinctrl_dev *pctldev, unsigned func_selector,
			  unsigned group_selector)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);
	struct aximux_function *func = &amx->functions[func_selector];
	struct aximux_group *grp = &amx->groups[group_selector];
	unsigned i, g;

	for (g = 0; g < func->ngroups; g++) {
		if (!strcmp(func->groups[g], grp->name))
			break;
	}
	if (g == func->ngroups)
		return -EINVAL;
	if (func->mux_npins[g] != grp->npins)
		return -EINVAL;

	for (i = 0; i < grp->npins; i++) {
		unsigned pin = grp->pins[i];
		unsigned src = func->mux_vals[g][i];

		if (pin >= AXIMUX_MAX_PINS || src > amx->nalts)
			return -EINVAL;
		aximux_set_src(amx, pin, src);
	}
	return 0;
}

static const struct pinmux_ops aximux_pinmux_ops = {
	.get_functions_count = aximux_get_functions_count,
	.get_function_name = aximux_get_function_name,
	.get_function_groups = aximux_get_function_groups,
	.set_mux = aximux_set_mux,
	.strict = true,
};

static int aximux_pinconf_get(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *config)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 val = aximux_read(amx, pin);
	u16 arg;

	switch ((unsigned int)param) {
	case AXIMUX_CFG_SHORT:
		arg = !!(val & AXIMUX_SHORT_BIT);
		break;
	case AXIMUX_CFG_DIR_SW:
		arg = !!(val & AXIMUX_DIREN_BIT);
		break;
	case AXIMUX_CFG_DIR_OUT:
		arg = !!(val & AXIMUX_DIRCTL_BIT);
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int aximux_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *configs, unsigned num_configs)
{
	struct aximux *amx = pinctrl_dev_get_drvdata(pctldev);
	unsigned i;

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param = pinconf_to_config_param(configs[i]);
		u16 arg = pinconf_to_config_argument(configs[i]);
		u32 val = aximux_read(amx, pin);

		switch ((unsigned int)param) {
		case AXIMUX_CFG_SHORT:
			if (arg)
				val |= AXIMUX_SHORT_BIT;
			else
				val &= ~AXIMUX_SHORT_BIT;
			break;
		case AXIMUX_CFG_DIR_SW:
			if (arg)
				val |= AXIMUX_DIREN_BIT;
			else
				val &= ~AXIMUX_DIREN_BIT;
			break;
		case AXIMUX_CFG_DIR_OUT:
			if (arg)
				val |= AXIMUX_DIRCTL_BIT;
			else
				val &= ~AXIMUX_DIRCTL_BIT;
			break;
		default:
			return -ENOTSUPP;
		}
		aximux_write(amx, pin, val);
	}
	return 0;
}

static int aximux_pinconf_group_get(struct pinctrl_dev *pctldev,
				    unsigned group, unsigned long *config)
{
	const unsigned *pins;
	unsigned npins;
	int ret;

	ret = aximux_get_group_pins(pctldev, group, &pins, &npins);
	if (ret || !npins)
		return ret ? ret : -EINVAL;
	return aximux_pinconf_get(pctldev, pins[0], config);
}

static int aximux_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned group, unsigned long *configs,
				    unsigned num_configs)
{
	const unsigned *pins;
	unsigned npins, i;
	int ret;

	ret = aximux_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = aximux_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct pinconf_ops aximux_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = aximux_pinconf_get,
	.pin_config_set = aximux_pinconf_set,
	.pin_config_group_get = aximux_pinconf_group_get,
	.pin_config_group_set = aximux_pinconf_group_set,
};

static const struct pinconf_generic_params aximux_cfg_params[] = {
	{ "brunosmmm,short", AXIMUX_CFG_SHORT, 0 },
	{ "brunosmmm,dir-sw", AXIMUX_CFG_DIR_SW, 0 },
	{ "brunosmmm,dir-out", AXIMUX_CFG_DIR_OUT, 0 },
};

static int aximux_parse_pins(struct aximux *amx, struct device_node *np)
{
	struct device_node *child;
	unsigned idx = 0;
	u32 info, hw_pins, hw_alts;

	info = ioread32(amx->regs + AXIMUX_REG_MUXINFO);
	hw_pins = info & 0xff;
	hw_alts = (info >> 8) & 0xff;

	if (!hw_pins || hw_pins > AXIMUX_MAX_PINS) {
		dev_err(amx->dev, "invalid MUXINFO pins=%u\n", hw_pins);
		return -EINVAL;
	}
	if (!hw_alts || hw_alts > AXIMUX_MAX_ALTS)
		hw_alts = AXIMUX_MAX_ALTS;
	amx->nalts = hw_alts;

	for_each_available_child_of_node(np, child) {
		u32 reg;

		if (of_find_property(child, "brunosmmm,pins", NULL))
			continue;
		if (of_property_read_u32(child, "reg", &reg))
			continue;
		amx->npins++;
	}

	if (!amx->npins) {
		amx->npins = hw_pins;
		amx->pins = devm_kcalloc(amx->dev, amx->npins, sizeof(*amx->pins),
					GFP_KERNEL);
		amx->pindescs = devm_kcalloc(amx->dev, amx->npins,
					     sizeof(*amx->pindescs), GFP_KERNEL);
		if (!amx->pins || !amx->pindescs)
			return -ENOMEM;

		for (idx = 0; idx < amx->npins; idx++) {
			char *name = devm_kasprintf(amx->dev, GFP_KERNEL,
						    "pin%u", idx);

			if (!name)
				return -ENOMEM;
			amx->pins[idx].number = idx;
			amx->pins[idx].name = name;
			amx->pindescs[idx].number = idx;
			amx->pindescs[idx].name = name;
		}
		return 0;
	}

	if (amx->npins > hw_pins) {
		dev_err(amx->dev, "DT pin count %u > MUXINFO %u\n",
			amx->npins, hw_pins);
		return -EINVAL;
	}

	amx->pins = devm_kcalloc(amx->dev, amx->npins, sizeof(*amx->pins),
				 GFP_KERNEL);
	amx->pindescs = devm_kcalloc(amx->dev, amx->npins, sizeof(*amx->pindescs),
				     GFP_KERNEL);
	if (!amx->pins || !amx->pindescs)
		return -ENOMEM;

	idx = 0;
	for_each_available_child_of_node(np, child) {
		u32 reg;
		int n;
		const char **names;
		const char *prop = NULL;

		if (of_find_property(child, "brunosmmm,pins", NULL))
			continue;
		if (of_property_read_u32(child, "reg", &reg))
			continue;
		if (reg >= hw_pins)
			return -EINVAL;

		amx->pins[idx].number = reg;
		amx->pins[idx].name = child->name;

		if (of_find_property(child, "function-names", NULL))
			prop = "function-names";
		else if (of_find_property(child, "alternate-names", NULL))
			prop = "alternate-names";

		if (prop) {
			n = of_property_count_strings(child, prop);
			if (n < 0)
				return n;
			names = devm_kcalloc(amx->dev, n, sizeof(*names),
					     GFP_KERNEL);
			if (!names)
				return -ENOMEM;
			if (of_property_read_string_array(child, prop, names, n) < 0)
				return -EINVAL;
			amx->pins[idx].func_names = names;
			amx->pins[idx].nfuncs = n;
		}

		amx->pindescs[idx].number = reg;
		amx->pindescs[idx].name = amx->pins[idx].name;
		idx++;
	}

	return 0;
}

static int aximux_build_default_groups(struct aximux *amx)
{
	unsigned i, f;
	int ret;

	amx->groups = kcalloc(amx->npins, sizeof(*amx->groups), GFP_KERNEL);
	if (!amx->groups)
		return -ENOMEM;
	amx->ngroups = amx->npins;

	for (i = 0; i < amx->npins; i++) {
		unsigned *pins = kmalloc(sizeof(*pins), GFP_KERNEL);

		if (!pins)
			return -ENOMEM;
		pins[0] = amx->pins[i].number;
		amx->groups[i].name = amx->pins[i].name;
		amx->groups[i].pins = pins;
		amx->groups[i].npins = 1;

		for (f = 0; f < amx->pins[i].nfuncs; f++) {
			unsigned mux = f;

			ret = aximux_add_function(amx, amx->pins[i].func_names[f],
						  amx->groups[i].name, 1, &mux);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int aximux_parse_extra_groups(struct aximux *amx, struct device_node *np)
{
	struct device_node *child;

	for_each_available_child_of_node(np, child) {
		int n, i, ret;
		unsigned *pins;
		unsigned *mux;
		const char *fname;
		struct aximux_group *newg, *grp;

		n = of_property_count_strings(child, "brunosmmm,pins");
		if (n <= 0)
			continue;

		pins = kmalloc_array(n, sizeof(*pins), GFP_KERNEL);
		mux = kcalloc(n, sizeof(*mux), GFP_KERNEL);
		if (!pins || !mux) {
			kfree(pins);
			kfree(mux);
			return -ENOMEM;
		}

		for (i = 0; i < n; i++) {
			const char *pname;
			int pin;

			ret = of_property_read_string_index(child, "brunosmmm,pins",
							    i, &pname);
			if (ret)
				goto err;
			pin = aximux_find_pin_by_name(amx, pname);
			if (pin < 0) {
				dev_err(amx->dev, "unknown pin '%s' in %pOFn\n",
					pname, child);
				ret = -EINVAL;
				goto err;
			}
			pins[i] = pin;
		}

		if (of_property_read_u32_array(child, "brunosmmm,mux", mux, n)) {
			for (i = 0; i < n; i++)
				mux[i] = 0;
		}

		if (of_property_read_string(child, "brunosmmm,function", &fname))
			fname = child->name;

		newg = krealloc(amx->groups,
				(amx->ngroups + 1) * sizeof(*amx->groups),
				GFP_KERNEL);
		if (!newg) {
			ret = -ENOMEM;
			goto err;
		}
		amx->groups = newg;
		grp = &amx->groups[amx->ngroups];
		grp->name = child->name;
		grp->pins = pins;
		grp->npins = n;
		amx->ngroups++;

		ret = aximux_add_function(amx, fname, child->name, n, mux);
		kfree(mux);
		if (ret)
			return ret;
		continue;
err:
		kfree(pins);
		kfree(mux);
		of_node_put(child);
		return ret;
	}
	return 0;
}

static ssize_t srcsel_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct aximux *amx = dev_get_drvdata(dev);
	unsigned i;
	int len = 0;

	for (i = 0; i < amx->npins; i++) {
		u32 val = aximux_read(amx, amx->pins[i].number);

		len += scnprintf(buf + len, PAGE_SIZE - len, "%s:0x%02x\n",
				 amx->pins[i].name, val & 0xff);
	}
	return len;
}
static DEVICE_ATTR_RO(srcsel);

static struct attribute *aximux_attrs[] = {
	&dev_attr_srcsel.attr,
	NULL,
};
ATTRIBUTE_GROUPS(aximux);

static int aximux_probe(struct platform_device *pdev)
{
	struct aximux *amx;
	struct pinctrl_desc *desc;
	struct resource *res;
	int ret;

	amx = devm_kzalloc(&pdev->dev, sizeof(*amx), GFP_KERNEL);
	if (!amx)
		return -ENOMEM;

	amx->dev = &pdev->dev;
	platform_set_drvdata(pdev, amx);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	amx->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(amx->regs))
		return PTR_ERR(amx->regs);

	ret = aximux_parse_pins(amx, pdev->dev.of_node);
	if (ret)
		return ret;

	ret = aximux_build_default_groups(amx);
	if (ret)
		return ret;

	ret = aximux_parse_extra_groups(amx, pdev->dev.of_node);
	if (ret)
		return ret;

	desc = devm_kzalloc(&pdev->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->name = dev_name(&pdev->dev);
	desc->owner = THIS_MODULE;
	desc->pins = amx->pindescs;
	desc->npins = amx->npins;
	desc->pctlops = &aximux_pinctrl_ops;
	desc->pmxops = &aximux_pinmux_ops;
	desc->confops = &aximux_pinconf_ops;
	desc->custom_params = aximux_cfg_params;
	desc->num_custom_params = ARRAY_SIZE(aximux_cfg_params);

	ret = devm_pinctrl_register_and_init(&pdev->dev, desc, amx, &amx->pctl);
	if (ret) {
		dev_err(&pdev->dev, "failed to register pinctrl: %d\n", ret);
		return ret;
	}

	ret = pinctrl_enable(amx->pctl);
	if (ret)
		return ret;

	dev_info(&pdev->dev,
		 "AXIMUX pinctrl ready: %u pins, %u groups, %u functions\n",
		 amx->npins, amx->ngroups, amx->nfunctions);
	return 0;
}

static const struct of_device_id aximux_of_match[] = {
	{ .compatible = "brunosmmm,aximux-2.0" },
	{ .compatible = "axi-mux-2.0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, aximux_of_match);

static struct platform_driver aximux_driver = {
	.probe = aximux_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = aximux_of_match,
		.dev_groups = aximux_groups,
	},
};

module_platform_driver(aximux_driver);

MODULE_AUTHOR("Bruno Morais <brunosmmm@gmail.com>");
MODULE_DESCRIPTION("AXI Mux (AXIMUX) pin controller driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
