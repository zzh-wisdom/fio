/**
 * dhms engine
 *
 * 采用read/write读写文件到指定的dsm_addr中
 *
 */
#include <dhms.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "../fio.h"
#include "../optgroup.h"
#include "../verify.h"

struct dhms_options_values {
  void *pad;
  char *conf_file;
};

static struct fio_option options[] = {
    {
        .name = "conf",
        .lname = "configure file",
        .type = FIO_OPT_STR_STORE,
        .off1 = offsetof(struct dhms_options_values, conf_file),
        .help = "Client's configure file",
        .category = FIO_OPT_C_ENGINE,
        .group = FIO_OPT_G_DHMS,
    },
    {
        .name = NULL,
    }};

struct dhms_data {
  struct dhms_pool *pool;
  size_t ws_size;
  size_t bs;
  dhms_addr *addrs;
  int num_addrs;
};

#define OBJ_MAX_SIZE (2L << 20)

static int fio_dhms_init(struct thread_data *td) {
  struct dhms_data *dd;
  struct thread_options *o = &td->o;
  struct dhms_options_values *eo = td->eo;

  dd = td->io_ops_data;
  if (!dd) {
    dd = malloc(sizeof(*dd));
    assert(dd);
    memset(dd, 0, sizeof(*dd));

    dd->ws_size = o->size;
    dd->bs = o->bs[DDIR_WRITE];
    assert(dd->bs);

    dd->num_addrs = dd->ws_size / dd->bs;
    assert(dd->num_addrs);
    dd->addrs = malloc(dd->num_addrs * sizeof(dhms_addr));
    assert(dd->addrs);

    // 预分配GAddr
    dd->pool = dhms_create(eo->conf_file, "fio-pool");
    for (int i = 0; i < dd->num_addrs; i++) {
      dd->addrs[i] = dhms_alloc(dd->pool, o->bs[DDIR_WRITE]);
      // 预写
      dhms_write(dd->pool, dd->addrs[i], &i, sizeof(i));
    }

    td->io_ops_data = dd;
  }
  return 0;
}

int fio_dhms_get_file_size(struct thread_data *td, struct fio_file *f) {
  struct dhms_data *dd = td->io_ops_data;

  f->real_file_size = dd->ws_size;
  fio_file_set_size_known(f);

  return 0;
}

static enum fio_q_status fio_dhms_queue(struct thread_data *td,
                                        struct io_u *io_u) {
  struct dhms_data *dd = td->io_ops_data;
  int ret = 0;
  int idx = io_u->offset / dd->bs % dd->num_addrs;

  if (io_u->ddir == DDIR_READ) {
    ret =
        dhms_read(dd->pool, dd->addrs[idx], io_u->xfer_buf, io_u->xfer_buflen);
  } else if (io_u->ddir == DDIR_WRITE) {
    ret =
        dhms_write(dd->pool, dd->addrs[idx], io_u->xfer_buf, io_u->xfer_buflen);
  } else {
    log_err("dhms: Invalid I/O Operation: %d\n", io_u->ddir);
    ret = EINVAL;
  }
  if (ret != (int)io_u->xfer_buflen) {
    if (ret >= 0) {
      io_u->resid = io_u->xfer_buflen - ret;
      io_u->error = 0;
      return FIO_Q_COMPLETED;
    } else {
      io_u->error = errno;
    }
  }
  if (io_u->error) td_verror(td, io_u->error, "xfer");
  return FIO_Q_COMPLETED;
}

static int fio_dhms_open_file(struct thread_data *td, struct fio_file *f) {
  fprintf(stderr, "[%s] \n", __func__);
  return 0;
}

static int fio_dhms_close_file(struct thread_data *td, struct fio_file *f) {
  fprintf(stderr, "[%s] \n", __func__);
  return 0;
}

static struct ioengine_ops ioengine = {
    .name = "dhms",
    .version = FIO_IOOPS_VERSION,
    .init = fio_dhms_init,
    .get_file_size = fio_dhms_get_file_size,
    .queue = fio_dhms_queue,
    .open_file = fio_dhms_open_file,
    .close_file = fio_dhms_close_file,
    .flags = FIO_DISKLESSIO | FIO_SYNCIO,
    .options = options,
    .option_struct_size = sizeof(struct dhms_options_values),
};

static void fio_init fio_dhms_register(void) {
  register_ioengine(&ioengine);
}

static void fio_exit fio_dhms_unregister(void) {
  unregister_ioengine(&ioengine);
}