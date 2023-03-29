#include <linux/io.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/printk.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/pinctrl/pinctrl.h>

#define DRIVER_NAME "aximux"

#define ADDR_LSB 2
#define AXIMUX_REG_OFFSET(x) (x<<ADDR_LSB)

//registers
#define AXIMUX_REG_MUXINFO 0x80
#define AXIMUX_REGOFF_SEL 0 ///< Source select register
#define AXIMUX_REGOFF_SHORT 5 ///< Short select register
#define AXIMUX_REGOFF_DIREN 6
#define AXIMUX_REGOFF_DIRCTL 7

//register bits
#define AXIMUX_SEL_MASK 0xFF

//driver flags
#define AXIMUX_FLAGS_INITIALIZED 0x01
#define AXIMUX_FLAGS_MODIFIED 0x02

//other
#define AXIMUX_MAX_INSTANCES 32

//debug
#define AXIMUX_DEBUG 1

#define SIGNAL_NAME_LIMIT 16
#define SIGNAL_LIMIT 32
#define SIGNAL_ALT_LIMIT 16

#define PORT_HAS_HW_CONTROL 0x01
#define PORT_HAS_SW_CONTROL 0x02
#define PORT_HWSW_CONTROL 0x08
#define PORT_SW_DIR 0x04

struct aximux_port {
  unsigned int idx;
  char *signal_name;
  const char **alternate_names;

  unsigned int alternate_count;

  // TODO: default states via device-tree
  unsigned int default_source;
  unsigned int default_direction;

  // direction control flags
  unsigned int direction_flags;

  // HACK: store attrs here for reference later, there are 5 attrs
  struct device_attribute source;
  struct device_attribute name;
  struct device_attribute diren;
  struct device_attribute dirctl;
  struct device_attribute alternates;
};

//ualu instance struct
struct aximux_device
{
  void __iomem *regs;
  struct device *dev;
  struct device *proxy_dev;
  struct cdev cdev;

  // hardwired parameters
  u32 alt_sig_n;
  u32 sig_count;

  // runtime flags
  u32 driver_flags;
  u32 instance_number;

  //signals
  struct aximux_port *ports;
};

//global driver data structure
struct aximux_instance
{
  //driver instances
  struct aximux_device* driver_instances[AXIMUX_MAX_INSTANCES];

  u32 available_instances;
  u32 dev_major;
};

//bookeeping
static struct aximux_instance* aximux_device_data;

/* Read / Write Registers */
static inline void reg_write(struct aximux_device *dev, u32 reg, u32 value)
{
  iowrite32(value, dev->regs + AXIMUX_REG_OFFSET(reg));
}

static inline u32 reg_read(struct aximux_device *dev, u32 reg)
{
  return ioread32(dev->regs + AXIMUX_REG_OFFSET(reg));
}

/* Perform Low-level device operations */
void aximux_set_src(struct aximux_device* dev, unsigned int port, unsigned int source)
{
  unsigned int cursrc = 0;
  unsigned int regval = 0;
  if (!dev) {
    return;
  }
  // register offset is port #
  // maximum 4 bits for source (16 alternates)
  regval = reg_read(dev, port);
  cursrc = regval & 0x0F;
  regval &= ~(0x0F);
  regval |= source & 0xF;
  reg_write(dev, port, regval);
}
EXPORT_SYMBOL(aximux_set_src);

void aximux_get_src(struct aximux_device*dev, unsigned int port, unsigned int *source)
{
  if (!source || !dev)
    {
      return;
    }
  *source = reg_read(dev, port) & 0x0F;
}
EXPORT_SYMBOL(aximux_get_src);

/* SYS FS */
static ssize_t src_store(struct device* dev, struct device_attribute* attr,
                         const char* buf, size_t count)
{
  struct aximux_device* drv = dev_get_drvdata(dev);
  struct aximux_port* port;
  unsigned int value = 0;

  // hack to find port #

  //get value
  port = container_of(attr, struct aximux_port, source);

  if (value > port->alternate_count)
    {
      printk(KERN_ERR "AXI Mux: port %d (%s) only has %d alternates\n",
             port->idx, port->signal_name, port->alternate_count);
      return -EINVAL;
    }

  aximux_set_src(drv, port->idx, value);

  return count;
}

static ssize_t src_show(struct device* dev, struct device_attribute* attr, char* buf)
{
  struct aximux_device* drv = dev_get_drvdata(dev);
  struct aximux_port* port;
  unsigned int cur_src = 0;

  // hack to find port #
  port = container_of(attr, struct aximux_port, source);

  aximux_get_src(drv, port->idx, &cur_src);
  if (cur_src > port->alternate_count) {
      printk(KERN_ERR "AXI Mux: got invalid value from port %d (%s) selector\n", port->idx,
             port->signal_name);
      return -EINVAL;
  }

  return sprintf(buf, "%u\n", cur_src);
}

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
                        char *buf) {
  struct aximux_port *port;

  // hack to find port #
  port = container_of(attr, struct aximux_port, source);

  return sprintf(buf, "%s\n", port->signal_name);
}

static ssize_t diren_show(struct device *dev, struct device_attribute *attr,
                         char *buf) {
  struct aximux_port *port;
  struct aximux_device *drv = dev_get_drvdata(dev);
  unsigned int port_reg = 0;
  // TODO: confirm if HW is 1 / SW 0
  unsigned char is_hw;

  // hack to find port #
  port = container_of(attr, struct aximux_port, source);

  port_reg = reg_read(drv, port->idx);
  is_hw = (port_reg & (1<<AXIMUX_REGOFF_DIREN));

  return sprintf(buf, "%s\n", is_hw ? "HW" : "SW");
}

static ssize_t diren_store(struct device *dev, struct device_attribute *attr,
                           const char *buf, size_t count) {
  struct aximux_port *port;
  struct aximux_device *drv = dev_get_drvdata(dev);
  unsigned int port_reg = 0;

  // TODO compare input

  // hack to find port #
  port = container_of(attr, struct aximux_port, source);

  port_reg = reg_read(drv, port->idx);
  port_reg &=  ~(1 << AXIMUX_REGOFF_DIREN);
  // TODO do something, writeback
  reg_write(drv, port->idx, port_reg);

  return count;
}

static const struct attribute_group* aximux_inst_attr_groups[SIGNAL_LIMIT] = {
  NULL,
};

static struct class aximux_class = {
  .name = "aximux",
  .owner = THIS_MODULE,
};

//export some functions
int aximux_instance_count(void)
{
  return aximux_device_data->available_instances;
}
EXPORT_SYMBOL(aximux_instance_count);

static struct of_device_id aximux_of_ids[] = {
  { .compatible = "axi-mux-2.0", },
  {}
};
MODULE_DEVICE_TABLE(of, aximux_of_ids);

int allocate_port_attributes(struct device *dev, struct aximux_port *port,
                             struct attribute ***port_attrs)
{
  struct device_attribute name = {
    .attr = {.name = "name", .mode = S_IRUGO},
    .show = name_show};
  struct device_attribute alternate = {
    .attr = {.name = "alternates", .mode = S_IRUGO}};
  struct device_attribute diren = {
    .attr = {.name = "direction_control", .mode = S_IWUSR | S_IRUGO},
    .show = diren_show,
    .store = diren_store};
  struct device_attribute dirctl = {
    .attr = {.name = "direction", .mode = S_IWUSR | S_IRUGO}};
  struct device_attribute source = {
    .attr = {.name = "source", .mode = S_IWUSR | S_IRUGO},
    .show = src_show,
    .store = src_store};

  struct attribute *_port_attrs[6];

  // hide attributes if HW control only
  if ((port->direction_flags & PORT_HAS_HW_CONTROL) && (!(port->direction_flags & PORT_HAS_SW_CONTROL))) {
    diren.attr.mode = S_IRUGO;
    diren.store = NULL;
    _port_attrs[4] = NULL;
  } else {
    if (port->direction_flags & PORT_HAS_SW_CONTROL) {
      port->dirctl = dirctl;
      _port_attrs[4] = &port->dirctl.attr;
    }
    if (!(port->direction_flags & PORT_HAS_HW_CONTROL)) {
      diren.attr.mode = S_IRUGO;
      diren.store = NULL;
    }
  }

  port->name = name;
  port->alternates = alternate;
  port->source = source;
  port->diren = diren;

  _port_attrs[0] = &port->name.attr;
  _port_attrs[1] = &port->alternates.attr;
  _port_attrs[2] = &port->source.attr;
  _port_attrs[3] = &port->diren.attr;
  _port_attrs[5] = NULL;


  *port_attrs = _port_attrs;
  return 0;
}

// probe driver
static int aximux_probe(struct platform_device *pdev)
{
  struct device_node *node = pdev->dev.of_node, *child;
  struct aximux_device *dev;
  struct resource *io;
  int err = 0;
  unsigned int value = 0;
  unsigned int iter = 0, iter2 = 0;
  struct device *buf_inst;
  dev_t devno = MKDEV(aximux_device_data->dev_major, aximux_device_data->available_instances);
  unsigned int port_count = of_get_child_count(node);

  //allocate
  dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
  if (!dev)
    return -ENOMEM;

  dev->dev = &pdev->dev;

  //map I/O memory
  io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  dev->regs = devm_ioremap_resource(&pdev->dev, io);

  if (IS_ERR(dev->regs))
    return PTR_ERR(dev->regs);

  // get size
  value = reg_read(dev, AXIMUX_REG_MUXINFO);
  dev->alt_sig_n = (value & 0xFF00) >> 8;
  if (dev->alt_sig_n > SIGNAL_ALT_LIMIT) {
    dev->alt_sig_n = SIGNAL_ALT_LIMIT;
  }
  dev->sig_count = (value & 0xFF);
  if (dev->sig_count > SIGNAL_LIMIT) {
    dev->sig_count = SIGNAL_LIMIT;
  }

  if (port_count > dev->sig_count) {
    printk(KERN_WARNING
           "AXI Mux reports %d signals, but device-tree entry requests %d\n",
           dev->sig_count, port_count);
   port_count = dev->sig_count;
  } else {
    if (port_count < dev->sig_count) {
      dev->sig_count = port_count;
    }
  }
  printk(KERN_INFO "AXI Mux with %d ports\n", port_count);

  // allocate port structures
  dev->ports = devm_kzalloc(&pdev->dev, dev->sig_count*sizeof(struct aximux_port), GFP_KERNEL);
  iter = 0;
  for_each_child_of_node(node, child) {
    // read port signal name
    dev->ports[iter].idx = iter;
    dev->ports[iter].signal_name =
      devm_kzalloc(&pdev->dev, SIGNAL_NAME_LIMIT * sizeof(char), GFP_KERNEL);
    strncpy(dev->ports[iter].signal_name, child->name, SIGNAL_NAME_LIMIT);

    // read alternate signal names
    value = of_property_read_string_array(child, "alternate_names", NULL, dev->alt_sig_n);
    if (value < 0) {
      printk(KERN_ERR "AXI Mux: cannot read alternate signals from entry %s\n", child->name);
      return -ENOENT;
    }
    if (value > dev->alt_sig_n) {
      printk(KERN_WARNING "AXI Mux: too many alternate names in entry %s\n", child->name);
      value = dev->alt_sig_n;
    }

    // allocate alternate names
    dev->ports[iter].alternate_count = value;
    dev->ports[iter].alternate_names = devm_kzalloc(&pdev->dev, value*sizeof(char*), GFP_KERNEL);
    for (iter2=0; iter2 < value; iter2++) {
      dev->ports[iter].alternate_names[iter2] = devm_kzalloc(&pdev->dev, SIGNAL_NAME_LIMIT*sizeof(char), GFP_KERNEL);
    }
    // finally read
    err = of_property_read_string_array(child, "alternate_names", dev->ports[iter].alternate_names, value);
    if (err) {
      return -ENOENT;
    }

    // TODO: default values, default directions

    iter++;
  }

  //increment amount of available instances
  dev->instance_number = aximux_device_data->available_instances;

  aximux_device_data->driver_instances[aximux_device_data->available_instances] = dev;
  aximux_device_data->available_instances++;

  // populate sysfs entries depending on input width
  for (iter = 0; iter < dev->sig_count; iter++) {
    struct attribute_group *signal_group = devm_kzalloc(&pdev->dev, sizeof(struct attribute_group), GFP_KERNEL);
    char *grp_name = devm_kzalloc(&pdev->dev, 8 * sizeof(char), GFP_KERNEL);
    snprintf(grp_name, 8, "port%d", iter);
    signal_group->name = grp_name;
    // attributes: source, name, alternate_names
    err = allocate_port_attributes(&pdev->dev, &dev->ports[iter], &signal_group->attrs);
    if (err < 0) {
      printk(KERN_ERR "AXI Mux: failed to allocate port attributes\n");
      return err;
    }
    /* signal_group->attrs = port_attrs; */
    // populate attributes
    aximux_inst_attr_groups[iter] = signal_group;
  }

  //sysfs entries
  buf_inst = device_create_with_groups(&aximux_class, &pdev->dev,
                                       devno, dev,
                                       aximux_inst_attr_groups,
                                       "aximux%d", aximux_device_data->available_instances-1);

  if (IS_ERR(buf_inst))
   {
      return PTR_ERR(buf_inst);
   }

  //store device "proxy"
  dev->proxy_dev = buf_inst;
  platform_set_drvdata(pdev, dev);
  node->data = dev;

  printk(KERN_INFO "%s: initialized aximux #%d\n", DRIVER_NAME,
         aximux_device_data->available_instances-1);

  return 0;
}

static int aximux_remove(struct platform_device *pdev)
{
  struct aximux_device* dev = platform_get_drvdata(pdev);

  //decrement amount of available instances
  aximux_device_data->available_instances--;

  //unregister prody device
  if (dev->proxy_dev)
    {
      device_destroy(&aximux_class, MKDEV(aximux_device_data->dev_major, dev->instance_number));
    }

  printk(KERN_INFO "%s: aximux #%d removed\n",
         DRIVER_NAME,
         dev->instance_number);

  return 0;
}

static struct platform_driver aximux_driver = {
  .probe = aximux_probe,
  .remove = aximux_remove,
  .driver = {
    .name = DRIVER_NAME,
    .of_match_table = of_match_ptr(aximux_of_ids),
  },
};

static int __init aximux_init(void)
{
  int err = 0;
  dev_t devt = 0;

  //register ualu class
  class_register(&aximux_class);

  //initialize local data structures
  aximux_device_data = kzalloc(sizeof(struct aximux_instance), GFP_KERNEL);
  if (!aximux_device_data)
    {
      return -ENOMEM;
    }

  aximux_device_data->available_instances = 0;


  err = alloc_chrdev_region(&devt, 0, AXIMUX_MAX_INSTANCES, "aximux");
  if (err < 0)
    {
      return err;
    }
  aximux_device_data->dev_major = MAJOR(devt);

  //register platform driver
  platform_driver_register(&aximux_driver);

  return 0;
}

static void __exit aximux_exit(void)
{

  unregister_chrdev_region(MKDEV(aximux_device_data->dev_major, 0), aximux_device_data->available_instances);

  platform_driver_unregister(&aximux_driver);

  class_destroy(&aximux_class);

  //free all allocated instances
  kfree(aximux_device_data);

}

module_init(aximux_init);
module_exit(aximux_exit);

MODULE_AUTHOR("brunosmmm@gmail.com");
MODULE_DESCRIPTION("AXI-MUX driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:"DRIVER_NAME);
