#include <xen/generic.h>
#include <xen/public/io/console.h>
#include <xen/public/memory.h>
#include <xen/public/xen.h>
#include <xen/hvm.h>

#include <domain.h>

#include <init.h>
#include <kernel.h>
#include <string.h>

K_KERNEL_STACK_DEFINE(read_thrd_stack, 8192);

/*
 * Need to read from OUT ring in dom0, domU writes logs there
 * TODO: place this in separate driver
 */
static int read_from_ring(struct xencons_interface *intf, char *str, int len)
{
	int recv = 0;
	XENCONS_RING_IDX cons = intf->out_cons;
	XENCONS_RING_IDX prod = intf->out_prod;
	XENCONS_RING_IDX out_idx = 0;

	compiler_barrier();
	__ASSERT((prod - cons) <= sizeof(intf->out), "Invalid input ring buffer");

	while (cons != prod && recv < len) {
		out_idx = MASK_XENCONS_IDX(cons, intf->out);
		str[recv] = intf->out[out_idx];
		recv++;
		cons++;
	}

	compiler_barrier();
	intf->out_cons = cons;

	return recv;
}

void console_read_thrd(void *dom, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	char buffer[128];
	char out[128];
	const int buflen = 128;
	int recv;
	int nlpos = 0;
	struct xen_domain *domain = (struct xen_domain *)dom;

	compiler_barrier();
	while (!domain->console_thrd_stop) {
		k_sem_take(&domain->console_sem, K_FOREVER);

		do {
			memset(out, 0, buflen);
			memset(buffer, 0, buflen);
			recv = read_from_ring(domain->intf, buffer + nlpos,
					      sizeof(buffer) - nlpos - 1);
			if (recv) {
				memcpy(out, buffer, recv);
				// disable temporary
				//				printk("%s", buffer);
			}
		} while (recv);
	}
}

void evtchn_callback(void *priv)
{
	struct xen_domain *domain = (struct xen_domain *)priv;
	k_sem_give(&domain->console_sem);
}

int init_domain_console(struct xen_domain *domain)
{
	domain->local_console_evtchn =
		evtchn_bind_interdomain(domain->domid, domain->console_evtchn);

	k_sem_init(&domain->console_sem, 1, 1);

	printk("%s: bind evtchn %d as %d\n", __func__, domain->console_evtchn,
	       domain->local_console_evtchn);

	int rc = hvm_set_parameter(HVM_PARAM_CONSOLE_EVTCHN, domain->domid, domain->console_evtchn);
	if (rc) {
		printk("Failed to set domain console evtchn param, rc= %d\n", rc);
	}

	bind_event_channel(domain->local_console_evtchn, evtchn_callback, domain);

	return rc;
}

int start_domain_console(struct xen_domain *domain)
{
	if (domain->console_tid) {
		printk("Console thread is already running for this domain!\n");
		return -EBUSY;
	}

	k_sem_init(&domain->console_sem, 1, 1);
	domain->console_thrd_stop = false;
	domain->console_tid =
		k_thread_create(&domain->console_thrd, read_thrd_stack,
				K_KERNEL_STACK_SIZEOF(read_thrd_stack), console_read_thrd, domain,
				NULL, NULL, 7, 0, K_NO_WAIT);

	return 0;
}

int stop_domain_console(struct xen_domain *domain)
{
	int rc;

	if (!domain->console_tid) {
		printk("No console thread is running!\n");
		return -ESRCH;
	}

	unbind_event_channel(domain->local_console_evtchn);
	rc = evtchn_close(domain->local_console_evtchn);

	domain->console_thrd_stop = true;
	/* Send event to end read cycle */
	k_sem_give(&domain->console_sem);

	k_thread_join(&domain->console_thrd, K_FOREVER);
	domain->console_tid = NULL;
	return 0;
}
