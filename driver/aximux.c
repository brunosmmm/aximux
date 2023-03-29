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

  //signal names
  char** signals;
  char** alt_signals;
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
  if (!dev) {
    return;
  }
  cursrc = reg_read(dev, AXIMUX_REG_SEL);
  cursrc &= ~(1<<port);
  cursrc |= (source) ? (1<<port) : 0;
  reg_write(dev, AXIMUX_REG_SEL, cursrc & AXIMUX_SEL_MASK);
}
EXPORT_SYMBOL(aximux_set_src);

void aximux_get_src(struct aximux_device*dev, unsigned int port, unsigned int *source)
{
  if (!source || !dev)
    {
      return;
    }
  *source = (reg_read(dev, AXIMUX_REG_SEL) & (1<<port)) ? 1 : 0;
}
EXPORT_SYMBOL(aximux_get_src);

/* SYS FS */
static ssize_t src_store(struct device* dev, struct device_attribute* attr,
                         const char* buf, size_t count)
{
  struct aximux_device* drv = dev_get_drvdata(dev);
  unsigned int value = 0;
  unsigned int port = 0;

  // hack to find port #
  sscanf(attr->attr.name, "source%d", &port);

  //get value
  sscanf(buf, "%u", &value);

  if (value > AXIMUX_SEL_MASK)
    {
      return -EINVAL;
    }

  aximux_set_src(drv, port, value);

  return count;
}

static ssize_t src_show(struct device* dev, struct device_attribute* attr, char* buf)
{
  struct aximux_device* drv = dev_get_drvdata(dev);
  unsigned int cur_src = 0;
  unsigned int port = 0;

  // hack to find port #
  sscanf(attr->attr.name, "source%d", &port);

  aximux_get_src(drv, port, &cur_src);

  return sprintf(buf, "%u\n", cur_src);
}


// device attributes
/* static struct device_attribute src_attr = { */
/*   .attr = { */
/*     .name = "source", */
/*     .mode = S_IWUSR | S_IRUGO, */
/*   }, */
/*   .show = src_show, */
/*   .store = src_store, */
/* }; */
static struct attribute *aximux_attrs[8] = {
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
};

static struct attribute_group aximux_inst_attr_group = {
  .name = "control",
  .attrs = aximux_attrs,
};

static const struct attribute_group* aximux_inst_attr_groups[] = {
  &aximux_inst_attr_group,
  NULL
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
  { .compatible = "xlnx,axi-mux-1.0", },
  {}
};
MODULE_DEVICE_TABLE(of, aximux_of_ids);

// probe driver
static int aximux_probe(struct platform_device *pdev)
{
  struct device_node *node = pdev->dev.of_node;
  struct aximux_device *dev;
  struct resource *io;
  int err = 0;
  unsigned int value = 0;
  unsigned int iter = 0;
  struct device *buf_inst;
  dev_t devno = MKDEV(aximux_device_data->dev_major, aximux_device_data->available_instances);

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

  dev->signals = devm_kzalloc(&pdev->dev, dev->sig_count*sizeof(char*), GFP_KERNEL);
  for (iter = 0; iter < dev->signals; iter++) {
    dev->signals[iter] = devm_kzalloc(&pdev->dev, SIGNAL_NAME_LIMIT*sizeof(char), GFP_KERNEL);
  }

  // TODO
  /* dev->alt_sig_n = */
  /*     devm_kzalloc(&pdev->dev, dev->sig_count * sizeof(char *), GFP_KERNEL); */
  /* for (iter = 0; iter < dev->signals; iter++) { */
  /*   dev->signals[iter] = */
  /*       devm_kzalloc(&pdev->dev, SIGNAL_NAME_LIMIT * sizeof(char), GFP_KERNEL); */
  /* } */

  // read signal names from properties
  err = of_property_read_string_array(node, "signal-names", dev->signals, dev->sig_count);
  if (err < 0) {
    printk("cannot get signal names\n");
  }
  // TODO, annoying
  /* for (iter = 0; iter < dev->alt_sig_n; iter++) { */
  /*   char property_name[18]; */
  /*   snprintf(property_name, 18, "alternate%d-names", iter); */
  /*   err = of_property_read_string_array(node, property_name, dev->alt_signals, value); */
  /*   if (err < 0) { */
  /*     printk("cannot get alternate signal names\n"); */
  /*   } */
  /* } */

  //increment amount of available instances
  dev->instance_number = aximux_device_data->available_instances;

  aximux_device_data->driver_instances[aximux_device_data->available_instances] = dev;
  aximux_device_data->available_instances++;

  // populate sysfs entries depending on input width
  for (iter = 0; iter < dev->input_width; iter++) {
    struct device_attribute *src_attr = devm_kzalloc(&pdev->dev,
                                                     sizeof(struct device_attribute),
                                                     GFP_KERNEL);
    char *attr_name = devm_kzalloc(&pdev->dev, 8*sizeof(char), GFP_KERNEL);
    struct attribute attr = {
      .name = attr_name,
      .mode = S_IWUSR | S_IRUGO
    };
    snprintf(attr_name, 8, "source%d", iter);
    src_attr->attr = attr;
    src_attr->show = src_show;
    src_attr->store = src_store;

    // populate attributes
    aximux_attrs[iter] = &src_attr->attr;
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
