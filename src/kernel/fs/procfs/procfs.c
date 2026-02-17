#include "procfs.h"
#include "user/access.h"
#include <fs/fd.h>
#include <memory/heap/kheap.h>
#include <stdio.h>
#include <string.h>
#include <util/macro.h>
#include <errors.h>
#include <cpu.h>

// features
// create the ProcFS (fake) volume
procfs_t *procfs_create() {
    procfs_t *p = kmalloc(sizeof(procfs_t));
    memset(p, 0, sizeof(procfs_t));
    return p;
}

void procfs_destroy_proc(procfs_pcb_t *proc) {
    for (size_t t = 0; t < proc->tcb_count; t++) {
        procfs_tcb_t *thread = proc->tcbs[t];
        if (!thread) {
            continue;
        }
        kfree(thread->tinfo);
        kfree(thread);
    }
    kfree(proc->tcbs);

    kfree(proc->procinfo->data);
    kfree(proc->procinfo);
    kfree(proc);
}

// destroy the ProcFS (stil fake) volume
void procfs_destroy(procfs_t *procfs) {
    for (size_t p = 0; p < procfs->pcb_count; p++) {
        if (!procfs->procs[p]) {
            continue;
        }

        procfs_destroy_proc(procfs->procs[p]);
    }

    kfree(procfs->procs);
    kfree(procfs);
}

void procfs_info_add(procfs_info_t *info, char *buf, size_t offset, size_t size) {
    if (!info) {
        return;
    }

    char *d = info->data;

    info->size = ROUND_UP((size + offset) + 1, PROCFS_FILESZ);
    d          = krealloc(d, info->size);

    memcpy(&d[offset], buf, size);
    d[offset + size] = '\0';

    if (d != info->data) {
        info->data = d;
    }
}

// returns the offset into the file of the FIRST occurrence of `key`
int procfs_info_find(procfs_info_t *info, char *key) {
    if (!info || !info->data) {
        return -1;
    }

    int off = -1;

    char *occ = strstr(info->data, key);
    if (occ) {
        off = ((void *)occ - info->data);
    }

    return off;
}

// return the length of the line of the FIRST occurrence of `key`
int procfs_info_linelen(procfs_info_t *info, char *key) {
    if (!info || !info->data) {
        return -1;
    }

    int offset = procfs_info_find(info, key);

    if (offset < 0) {
        return -1;
    }

    char *buf = info->data;
    int len   = 0;

    while (buf[len + offset] != '\n') {
        len++;
    }

    return (len + 1);
}

void procfs_info_del(procfs_info_t *info, size_t offset, size_t size) {
    if (!info || !info->data) {
        return;
    }

    char *d  = info->data;
    size_t s = offset + size;
    if (s > info->size) {
        s = info->size - offset;
    }

    memcpy(&d[offset], &d[offset + size], s);

    s = (offset + size) - info->size;
    memset(&d[size + offset], 0, s);
}

// parse TCB attributes to write to a tinfo file
procfs_info_t *procfs_tinfo_create(tcb_t *tcb) {
    if (!tcb) {
        return NULL;
    }

    char *buf = kmalloc(PROCFS_INFO_BUFSZ);
    memset(buf, 0, PROCFS_INFO_BUFSZ);

    procfs_info_t *tinfo = kmalloc(sizeof(procfs_info_t));
    memset(tinfo, 0, sizeof(procfs_info_t));

    // this is veeeery wacky

    // flags
    snprintf(buf, PROCFS_INFO_BUFSZ, "TF_USER::%d\n",
             (tcb->flags & TF_MODE_USER) ? 1 : 0);
    procfs_info_add(tinfo, buf, 0, strlen(buf));
    int offset = procfs_info_linelen(tinfo, buf);

    snprintf(buf, PROCFS_INFO_BUFSZ, "TF_BUSY::%d\n",
             (tcb->flags & TF_BUSY) ? 1 : 0);
    procfs_info_add(tinfo, buf, offset, strlen(buf));
    offset += procfs_info_linelen(tinfo, buf);

    snprintf(buf, PROCFS_INFO_BUFSZ, "TF_DETACHED::%d\n",
             (tcb->flags & TF_DETACHED) ? 1 : 0);
    procfs_info_add(tinfo, buf, offset, strlen(buf));
    offset += procfs_info_linelen(tinfo, buf);

    if (tcb->regs) {
        // entry point (suppose we are called first on process creation)
        snprintf(buf, PROCFS_INFO_BUFSZ, "ENTRY::%016llx\n", tcb->regs->rip);
        procfs_info_add(tinfo, buf, offset, strlen(buf));
        offset += procfs_info_linelen(tinfo, buf);

        // stack
        snprintf(buf, PROCFS_INFO_BUFSZ, "STACK::%016llx\n", tcb->regs->rbp);
        procfs_info_add(tinfo, buf, offset, strlen(buf));
        offset += procfs_info_linelen(tinfo, buf);
    } else {
        debugf_warn("Sir we have a problem, this TCB(%p) doesn't have the regs "
                    "structure\n",
                    tcb);
    }

    tinfo->size = strlen(tinfo->data);

    return tinfo;
}

// parse PCB attributes to write to a procinfo file
procfs_info_t *procfs_procinfo_create(pcb_t *pcb) {
    if (!pcb) {
        return NULL;
    }

    char *buf = kmalloc(PROCFS_INFO_BUFSZ);
    memset(buf, 0, PROCFS_INFO_BUFSZ);

    procfs_info_t *procinfo = kmalloc(sizeof(procfs_info_t));
    memset(procinfo, 0, sizeof(procfs_info_t));

    // fd count
    snprintf(buf, PROCFS_INFO_BUFSZ, "FD_COUNT::%zu\n", pcb->fd_table.size);
    procfs_info_add(procinfo, buf, 0, strlen(buf) + 1);
    int offset = procfs_info_linelen(procinfo, buf);

    // tcb count
    snprintf(buf, PROCFS_INFO_BUFSZ, "TCB_COUNT::%d\n", pcb->thread_count);
    procfs_info_add(procinfo, buf, offset, strlen(buf) + 1);
    offset += procfs_info_linelen(procinfo, buf);

    // cpu where is currently running
    snprintf(buf, PROCFS_INFO_BUFSZ, "CURRENT_CPU::%d\n", get_current_cpu());
    procfs_info_add(procinfo, buf, offset, strlen(buf) + 1);
    offset += procfs_info_linelen(procinfo, buf);

    // signal handler? (later)

    procinfo->size = strlen(procinfo->data);

    kfree(buf);
    return procinfo;
}

// create a ProcFS tid_file (/proc/1/2)
procfs_tcb_t *procfs_thread_create(tcb_t *tcb) {
    if (!tcb) {
        return NULL;
    }

    procfs_tcb_t *procfs_tcb = kmalloc(sizeof(procfs_tcb_t));
    memset(procfs_tcb, 0, sizeof(procfs_tcb_t));

    procfs_tcb->tid = tcb->tid;
    procfs_tcb->tcb = tcb;

    // tinfo file
    procfs_info_t *tinfo = procfs_tinfo_create(tcb);
    procfs_tcb->tinfo    = tinfo;

    return procfs_tcb;
}

// create a ProcFS pid_file (eg. /proc/1)
procfs_pcb_t *procfs_proc_create(pcb_t *pcb) {
    if (!pcb) {
        return NULL;
    }

    procfs_pcb_t *procfs_pcb = kmalloc(sizeof(procfs_pcb_t));
    memset(procfs_pcb, 0, sizeof(procfs_pcb));

    // copy PCB info to structure
    procfs_pcb->pcb = pcb;
    procfs_pcb->pid = pcb->pid;
    // TODO: exec

    // procinfo file
    procfs_pcb->procinfo = procfs_procinfo_create(pcb);

    procfs_pcb->tcb_count = pcb->thread_count;
    procfs_pcb->tcbs = kcalloc(procfs_pcb->tcb_count, sizeof(procfs_tcb_t *));

    // create every single thread file
    for (size_t i = 0; i < procfs_pcb->tcb_count; i++) {
        procfs_pcb->tcbs[i] = procfs_thread_create(pcb->threads[i]);
    }

    return procfs_pcb;
}

void procfs_proc_append(procfs_t *procfs, procfs_pcb_t *proc) {
    if (!procfs || !proc) {
        return;
    }

    procfs->procs = krealloc(procfs->procs,
                            (sizeof(procfs_pcb_t *) * (++procfs->pcb_count)));
    procfs->procs[procfs->pcb_count - 1] = proc;
}

void procfs_proc_remove(procfs_t *procfs, int pid) {
    if (!procfs || !procfs->procs) {
        return;
    }

    for (int i = 0; i < procfs->pcb_count; i++) {
        if (!procfs->procs[i] || procfs->procs[i]->pid != pid) {
            continue;
        }

        procfs_destroy_proc(procfs->procs[i]);
        procfs->procs[i] = NULL;

        if (i == (procfs->pcb_count - 1)) {
            procfs->procs =
                krealloc(procfs->procs, (sizeof(procfs_pcb_t *) * i));
            procfs->pcb_count = i;
        }
    }
}

// find the pid given the procFS
procfs_pcb_t *procfs_find_proc(procfs_t *procfs, int pid) {
    if (!procfs || pid < 0) {
        return NULL;
    }

    for (size_t i = 0; i < procfs->pcb_count; i++) {
        if (procfs->procs[i] && procfs->procs[i]->pid == pid) {
            return procfs->procs[i];
        }
    }

    return NULL;
}

// find the tid given the procFS
procfs_tcb_t *procfs_find_thread(procfs_t *procfs, int pid, int tid) {
    if (!procfs || pid < 0 || tid < 0) {
        return NULL;
    }

    procfs_pcb_t *proc = procfs_find_proc(procfs, pid);

    if (!proc) {
        return NULL;
    }

    for (size_t i = 0; i < proc->tcb_count; i++) {
        if (proc->tcbs[i] && proc->tcbs[i]->tid == tid) {
            return proc->tcbs[i];
        }
    }

    return NULL;
}

// find the tid given the procFS FILE
procfs_tcb_t *procfs_find_thread_from_proc(procfs_pcb_t *proc, int tid) {
    if (!proc || tid < 0) {
        return NULL;
    }

    for (size_t i = 0; i < proc->tcb_count; i++) {
        if (proc->tcbs[i] && proc->tcbs[i]->tid == tid) {
            return proc->tcbs[i];
        }
    }

    return NULL;
}

// /proc/p can contain many files
// /proc/p/t SHOULD contain only the tinfo tile

// find a file INSIDE a /proc/p "dir"
void *procfs_find_in_proc(procfs_pcb_t *proc, char *file, int *ftype) {
    if (!proc || !file) {
        return NULL;
    }

    void *f = NULL;

    // bunch of file names
    if (strcmp(file, PROCFS_FNAME_PROCINFO) == 0) {
        *ftype = PROCFS_FILE_INFO;
        f      = proc->procinfo;
    } else if (strcmp(file, PROCFS_FNAME_FDS) == 0) {
        *ftype = PROCFS_FILE_FDS;
        f      = proc;
    } else {
        int tid = atoi(file);

        f      = procfs_find_thread_from_proc(proc, tid);
        *ftype = PROCFS_FILE_THREAD;
    }

    return f;
}

// ProcFS path parsing
// eg.      /proc/1 would return that the path points to a proc file
//          /proc/1/4/tinfo returns the tinfo file
//          /proc/self should return the current process

// @param ftype we will write the file type we found with the given path
// cuz we are returning a void*
void *procfs_find(procfs_t *procfs, char *path, int *ftype) {
    if (!procfs || !path || !ftype) {
        return NULL;
    }

    if (strncmp(path, "/proc/", strlen("/proc/")) == 0) {
        path += strlen("/proc/");
    }

    char *name_dup = strdup(path);
    char *temp     = name_dup;
    char *file;

    void *f = NULL;
    int pid;
    procfs_pcb_t *proc = NULL;
    int tid;

    // i -> depth into the FS
    // 0 -> we are in the procFS root
    // 1 -> we are in a pid file
    // 2 -> we are in a thread file
    // anything else is illegal :3c
    for (int i = 0; *temp; i++) {
        file = strtok_r(NULL, "/", &temp);

        switch (i) {
        case 0:
            // there are only PIDs or the /self in the procFS root
            if (strcmp(file, "self") == 0) {
                pcb_t *p = get_current_pcb();
                proc     = procfs_find_proc(procfs, p->pid);
            } else {
                pid  = atoi(file);
                proc = procfs_find_proc(procfs, pid);
            }

            if (!(*temp)) {
                *ftype = PROCFS_FILE_PROC;
                return proc;
            }
            break;

        case 1:
            // inside /proc/p there are many files
            f = procfs_find_in_proc(proc, file, ftype);

            if (!(*temp)) {
                return f;
            }

        case 2:
            // there's only the tinfo file (for now)
            procfs_tcb_t *tcb = f;

            return tcb->tinfo;

        default:
            debugf_panic("WHY ARE WE HERE LOL\n");
            break;
        }
    }

    return f;
}

size_t procfs_get_node_size(procfs_vnode_t *pvnode) {
    if (!pvnode) {
        return 0;
    }

    size_t s = 0;

    switch (pvnode->type) {
    case PROCFS_FILE_INFO:
        s = ((procfs_info_t *)pvnode->file)->size;
        break;

    default:
        break;
    }
    
    return s;
}

void *procfs_get_node_buf(procfs_vnode_t *pvnode) {
    if (!pvnode) {
        return NULL;
    }

    void *p = NULL;

    switch (pvnode->type) {
    case PROCFS_FILE_INFO:
        p = ((procfs_info_t *)pvnode->file)->data;
        break;

    default:
        break;
    }
    
    return p;
}

// vnode functions (open, read, write, close)

int procfs_open(vnode_t **vout, int flags, bool clone, fileio_t **fout) {
    UNUSED(clone); // TODO: use the clone :kekw:

    if (!vout || !(*vout)) {
        return EFAULT;
    }

    vnode_t *vnode   = *vout;
    procfs_t *procfs = vnode->root_vfs->vfs_data;

    // find procfs file
    int ftype;
    void *file = procfs_find(procfs, vnode->path, &ftype);
    if (!file) {
        // we won't create the file :3
        return ENOENT;
    }

    procfs_vnode_t *pvnode = kmalloc(sizeof(procfs_vnode_t));
    memset(pvnode, 0, sizeof(procfs_vnode_t));
    pvnode->file = file;
    pvnode->type = ftype;

    vnode->node_data = pvnode;

    // create fio_t struct
    fileio_t *fio = *fout;
    if (!fout || !fio) {
        return EFAULT;
    }

    fio->buf_start = procfs_get_node_buf(pvnode);
    fio->size      = procfs_get_node_size(pvnode);
    fio->private   = vnode;

    return EOK;
}

int procfs_read(vnode_t *vnode, size_t *size, size_t *offset, void *out) {
    // depends on the file

    if (!vnode) {
        return -EFAULT;
    }

    memset(out, 0, (*size));

    procfs_vnode_t *pvnode = vnode->node_data;
    void *buf              = procfs_get_node_buf(pvnode);
    size_t buf_size        = procfs_get_node_size(pvnode);

    if ((*size) > buf_size) {
        (*size) = buf_size;
    } else if ((*offset) >= buf_size) {
        return EINVAL;
    }

    if ((*size) + (*offset) > buf_size) {
        (*size) = (buf_size - (*offset));
    }

    void *src = buf + (*offset);

    memcpy(out, src, (*size));

    return EOK;
}

int procfs_write(vnode_t *vnode, void *buf, size_t *size, size_t *offset) {
    UNUSED(vnode);
    UNUSED(buf);
    UNUSED(size);
    UNUSED(offset);

    debugf_warn("STOP! YOU VIOLATED THE LAW!\n");
    return EACCES;
}

int procfs_close(vnode_t *vnode, int flags, bool clone) {
    UNUSED(flags);
    UNUSED(clone);

    if (!vnode) {
        return EFAULT;
    }

    procfs_vnode_t *pvnode = vnode->node_data;
    if (!pvnode) {
        return EFAULT;
    }

    kfree(vnode->node_data);
    vnode->node_data = NULL;

    return EOK;
}

int procfs_ioctl(vnode_t *vnode, int request, void *arg) {
    if (!vnode) {
        return EFAULT;
    }

    UNUSED(request);
    UNUSED(arg);

    return ENOSYS;
}

int procfs_lookup(vnode_t *parent, const char *name, vnode_t **out) {
    if (!parent || !name || !out) {
        return EFAULT;
    }

    procfs_t *procfs = parent->root_vfs->vfs_data;
    if (!procfs) {
        return EFAULT;
    }

    char *rel_path = parent->path + strlen(parent->root_vfs->root_vnode->path);
    if (rel_path[0] == '/') rel_path++;

    size_t parent_len = strlen(parent->path);
    size_t name_len = strlen(name);
    char *child_path = kmalloc(parent_len + name_len + 2);
    strcpy(child_path, parent->path);
    if (child_path[parent_len - 1] != '/') {
    	strcat(child_path, "/");
    }
    strcat(child_path, name);

	size_t path_len = strlen(child_path);
	if (path_len >= 4 && strcmp(child_path + path_len - 4, "/fds") == 0) {
    	size_t proc_path_len = path_len - 4;
    	char *proc_path = kmalloc(proc_path_len + 1);
    	strncpy(proc_path, child_path, proc_path_len);
    	proc_path[proc_path_len] = '\0';
    
    	int ftype;
    	void *file = procfs_find(procfs, proc_path, &ftype);
    	kfree(proc_path);
    
    	if (!file || ftype != PROCFS_FILE_PROC) {
        	kfree(child_path);
        	return ENOENT;
    	}
    
    	procfs_pcb_t *proc = (procfs_pcb_t *)file;
    
    	vnode_t *child_vnode = vnode_create(parent->root_vfs, child_path, VNODE_DIR, proc);
    	memcpy(child_vnode->ops, parent->ops, sizeof(vnops_t));
    	child_vnode->mode = S_IFDIR | 0555;  // Add mode for /fds directory
    
    	*out = child_vnode;
    	return EOK;
	}

	char *fds_pos = NULL;
	for (size_t i = 0; i < strlen(child_path) - 4; i++) {
    	if (child_path[i] == '/' && child_path[i+1] == 'f' && 
        	child_path[i+2] == 'd' && child_path[i+3] == 's' && 
        	child_path[i+4] == '/') {
        	fds_pos = child_path + i;
        	break;
    	}
	}

	if (fds_pos != NULL) {
    	int fd_num = atoi(fds_pos + 5);

    	size_t proc_path_len = fds_pos - child_path;
    	char *proc_path = kmalloc(proc_path_len + 1);
    	strncpy(proc_path, child_path, proc_path_len);
    	proc_path[proc_path_len] = '\0';
    
    	int ftype;
    	void *file = procfs_find(procfs, proc_path, &ftype);
    	kfree(proc_path);       
        if (!file || ftype != PROCFS_FILE_PROC) {
            kfree(child_path);
            return ENOENT;
        }
        
        procfs_pcb_t *proc = (procfs_pcb_t *)file;
        
        fileio_t *fio = fd_get(&proc->pcb->fd_table, fd_num, FD_FILE);
        if (!fio) {
            kfree(child_path);
            return ENOENT;
        }
        
        vnode_t *child_vnode = vnode_create(parent->root_vfs, child_path, VNODE_LINK, fio);
        memcpy(child_vnode->ops, parent->ops, sizeof(vnops_t));
        child_vnode->mode = S_IFLNK | 0777;
        
        *out = child_vnode;
        return EOK;
    }

    int ftype;
    void *file = procfs_find(procfs, child_path, &ftype);
    
    if (!file) {
        kfree(child_path);
        return ENOENT;
    }

    vnode_type_t vtype = VNODE_REGULAR;
    mode_t mode = S_IFREG | 0444;  // Default: regular file, read-only
    
    if (ftype == PROCFS_FILE_PROC || ftype == PROCFS_FILE_THREAD) {
        vtype = VNODE_DIR;
        mode = S_IFDIR | 0555;
    } else if (ftype == PROCFS_FILE_FDS) {
        vtype = VNODE_DIR;
        mode = S_IFDIR | 0555;
	}

    vnode_t *child_vnode = vnode_create(parent->root_vfs, child_path, vtype, file);
    memcpy(child_vnode->ops, parent->ops, sizeof(vnops_t));
    child_vnode->mode = mode;  // Set the mode
    
    *out = child_vnode;
    return EOK;
}

int procfs_readdir(vnode_t *vnode, dirent_t *entries, size_t *count) {
    if (!vnode || !entries || !count) {
        return EFAULT;
    }
    if (vnode->vtype != VNODE_DIR) {
        return ENOTDIR;
    }

    procfs_t *procfs = vnode->root_vfs->vfs_data;
    if (!procfs) {
        return EFAULT;
    }

    char *rel_path = vnode->path + strlen(vnode->root_vfs->root_vnode->path);
    if (rel_path[0] == '/') rel_path++;

    size_t idx = 0;
    size_t max = *count;

    if (rel_path[0] == '\0') {
        for (size_t i = 0; i < procfs->pcb_count && idx < max; i++) {
            if (!procfs->procs[i]) continue;

            entries[idx].d_ino = procfs->procs[i]->pid;
            entries[idx].d_off = idx + 1;
            entries[idx].d_reclen = sizeof(dirent_t);
            entries[idx].d_type = VNODE_DIR;
            snprintf(entries[idx].d_name, sizeof(entries[idx].d_name), "%lu",
                     procfs->procs[i]->pid);
            idx++;
        }

        if (idx < max) {
            entries[idx].d_ino = 0;
            entries[idx].d_off = idx + 1;
            entries[idx].d_reclen = sizeof(dirent_t);
            entries[idx].d_type = VNODE_DIR;
            strncpy(entries[idx].d_name, "self", sizeof(entries[idx].d_name) - 1);
            idx++;
        }
    } else {
        int ftype;
        void *file = procfs_find(procfs, vnode->path, &ftype);

        if (ftype == PROCFS_FILE_PROC) {
            procfs_pcb_t *proc = (procfs_pcb_t *)file;

            if (idx < max) {
                entries[idx].d_ino = 0;
                entries[idx].d_off = idx + 1;
                entries[idx].d_reclen = sizeof(dirent_t);
                entries[idx].d_type = VNODE_REGULAR;
                strncpy(entries[idx].d_name, PROCFS_FNAME_PROCINFO,
                        sizeof(entries[idx].d_name) - 1);
                idx++;
            }

            if (idx < max) {
                entries[idx].d_ino = 0;
                entries[idx].d_off = idx + 1;
                entries[idx].d_reclen = sizeof(dirent_t);
                entries[idx].d_type = VNODE_DIR;
                strncpy(entries[idx].d_name, PROCFS_FNAME_FDS,
                        sizeof(entries[idx].d_name) - 1);
                idx++;
            }

            for (size_t i = 0; i < proc->tcb_count && idx < max; i++) {
                if (!proc->tcbs[i]) continue;

                entries[idx].d_ino = proc->tcbs[i]->tid;
                entries[idx].d_off = idx + 1;
                entries[idx].d_reclen = sizeof(dirent_t);
                entries[idx].d_type = VNODE_DIR;
                snprintf(entries[idx].d_name, sizeof(entries[idx].d_name), "%lu",
                         proc->tcbs[i]->tid);
                idx++;
            }
        }

        else if (ftype == PROCFS_FILE_THREAD) {
            if (idx < max) {
                entries[idx].d_ino = 0;
                entries[idx].d_off = idx + 1;
                entries[idx].d_reclen = sizeof(dirent_t);
                entries[idx].d_type = VNODE_REGULAR;
                strncpy(entries[idx].d_name, PROCFS_FNAME_TINFO,
                        sizeof(entries[idx].d_name) - 1);
                idx++;
            }
        
		}
        else {
            size_t path_len = strlen(vnode->path);
            if (path_len >= 4 && strcmp(vnode->path + path_len - 4, "/fds") == 0) {
                procfs_pcb_t *proc = (procfs_pcb_t *)vnode->node_data;
                if (!proc || !proc->pcb) {
                    *count = 0;
                    return EOK;
                }

                for (size_t i = 0; i < proc->pcb->fd_table.size && idx < max; i++) {
                    if (proc->pcb->fd_table.entries[i].type == FD_NONE) continue;

                    entries[idx].d_ino = i;
                    entries[idx].d_off = idx + 1;
                    entries[idx].d_reclen = sizeof(dirent_t);
                    entries[idx].d_type = VNODE_LINK;
                    snprintf(entries[idx].d_name, sizeof(entries[idx].d_name), "%zu", i);
                    idx++;
                }
            }
        }
    }

    *count = idx;
    return EOK;
}

int procfs_readlink(vnode_t *vnode, char *buf, size_t size) {
	if (!vnode || !buf) {
        return EFAULT;
    }

    if (vnode->vtype != VNODE_LINK) {
        return EINVAL;
    }

    fileio_t *fio = (fileio_t *)vnode->node_data;
    if (!fio || !fio->private) {
        strncpy(buf, "(unknown)", size - 1);
        buf[size - 1] = '\0';
        return EOK;
    }

    vnode_t *target_vnode = (vnode_t *)fio->private;
    if (!target_vnode || !target_vnode->path) {
        strncpy(buf, "(unknown)", size - 1);
        buf[size - 1] = '\0';
        return EOK;
    }

    strncpy(buf, target_vnode->path, size - 1);
    buf[size - 1] = '\0';

    return EOK;
}

static int procfs_mmap(vnode_t *vnode, void *addr, size_t length, int prot, int flags, size_t offset) {
    (void)vnode; (void)addr; (void)length; (void)prot; (void)flags; (void)offset;
    return ENOSYS; // you CANNOT mmap files in procfs, its a fake fs :3c
}

vnops_t procfs_vnops = {
    .open    = procfs_open,
    .close   = procfs_close,
    .read    = procfs_read,
    .write   = procfs_write,
    .ioctl   = procfs_ioctl,
    .lookup  = procfs_lookup,
    .readdir = procfs_readdir,
    .readlink = procfs_readlink,
    .mmap    = procfs_mmap,
};

static int procfs_vfs_mount(vfs_t *vfs, char *path, void *data) {
    UNUSED(path);
    UNUSED(data);
    
    if (!vfs) {
        return EFAULT;
    }
    
    return EOK;
}

static int procfs_vfs_unmount(vfs_t *vfs) {
    if (!vfs) {
        return EFAULT;
    }
    
    procfs_t *procfs = vfs->vfs_data;
    if (procfs) {
        procfs_destroy(procfs);
    }
    
    return EOK;
}

static int procfs_vfs_root(vfs_t *vfs, vnode_t **out) {
    if (!vfs || !out) {
        return EFAULT;
    }
    
    *out = vfs->root_vnode;
    vnode_ref(*out);
    
    return EOK;
}

static int procfs_vfs_statfs(vfs_t *vfs, statfs_t *stat) {
    if (!vfs || !stat) {
        return EFAULT;
    }
    
    procfs_t *procfs = vfs->vfs_data;
    if (!procfs) {
        return EFAULT;
    }
    
    stat->block_size = 1;
    stat->total_blocks = 0;
    stat->free_blocks = 0;
    stat->total_nodes = procfs->pcb_count;
    stat->free_nodes = 0;
    
    return EOK;
}

static int procfs_vfs_sync(vfs_t *vfs) {
    UNUSED(vfs);
    return EOK;
}

vfsops_t procfs_vfsops = {
    .mount   = procfs_vfs_mount,
    .unmount = procfs_vfs_unmount,
    .root    = procfs_vfs_root,
    .statfs  = procfs_vfs_statfs,
    .sync    = procfs_vfs_sync,
};

static int procfs_fstype_mount(void *device, char *mount_point, void *mount_data, vfs_t **out) {
    UNUSED(mount_data);
    
    procfs_t *procfs = (procfs_t *)device;
    if (!procfs) {
        procfs = procfs_create();
        if (!procfs) {
            return ENOMEM;
        }
    }
    
    vfs_fstype_t fstype;
    memset(&fstype, 0, sizeof(vfs_fstype_t));
    strncpy(fstype.name, "procfs", sizeof(fstype.name) - 1);
    
    vfs_t *vfs = vfs_create_fs(&fstype, procfs);
    if (!vfs) {
        return ENOMEM;
    }
    
    memcpy(vfs->ops, &procfs_vfsops, sizeof(vfsops_t));
    
    vfs->root_vnode = vnode_create(vfs, mount_point, VNODE_DIR, NULL);
    if (!vfs->root_vnode) {
        kfree(vfs->ops);
        kfree(vfs);
        return ENOMEM;
    }
    
    memcpy(vfs->root_vnode->ops, &procfs_vnops, sizeof(vnops_t));
    vfs->root_vnode->mode = S_IFDIR | 0555;
    
    *out = vfs;
    return EOK;
}

static vfs_fstype_t procfs_fstype = {
    .id = 0,
    .name = "procfs",
    .mount = procfs_fstype_mount,
    .next = NULL
};

void procfs_init(void) {
    vfs_register_fstype(&procfs_fstype);
}

int procfs_vfs_init(procfs_t *procfs, char *path) {
    if (!path) {
        return EFAULT;
    }

    vfs_t *vfs = vfs_mount(procfs, "procfs", path, NULL);
    if (!vfs) {
        return ENOTRECOVERABLE;
    }

    return EOK;
}

void procfs_print(procfs_t *procfs) {
    if (!procfs) {
        return;
    }

    for (int i = 0; i < procfs->pcb_count; i++) {
        procfs_pcb_t *p = procfs->procs[i];
        if (p) {
            kprintf("|- %d\n", p->pid);
            for (int j = 0; j < p->tcb_count; j++) {
                procfs_tcb_t *t = p->tcbs[j];
                if (t) {
                    kprintf("|\t|- %d\n", j);
                    kprintf("|\t|  |_ tinfo\n");
                }
            }
            if (p->pcb->fd_table.entries) {
                kprintf("|\t|-fds\n");
                for (size_t j = 0; j < p->pcb->fd_table.size; j++) {
                    if (p->pcb->fd_table.entries[j].type != FD_NONE) {
                        kprintf("|\t  |- %zu[%p]\n", j, p->pcb->fd_table.entries[j].ptr);
                    }
                }
            }
            kprintf("|\t|_procinfo\n");
        }
    }
}

void procfs_foreach(void (*callback)(procfs_t *procfs, void *arg), void *arg) {
    for (vfs_t *vfs = vfs_list; vfs != NULL; vfs = vfs->next) {
        if (vfs->fs_type.id == procfs_fstype.id && vfs->vfs_data) {
            procfs_t *procfs = (procfs_t *)vfs->vfs_data;
            callback(procfs, arg);
        }
    }
}

void procfs_remove_process_foreach(procfs_t *procfs, void *varg) {
	pcb_t *pcb = (pcb_t *)varg;
    procfs_proc_remove(procfs, pcb->pid);
}

void procfs_update_process_foreach(procfs_t* procfs, void *varg) {
	struct update_arg *a = (struct update_arg *)varg;
	pcb_t *pcb = a->pcb;

    procfs_pcb_t *entry = procfs_find_proc(procfs, pcb->pid);

    if (!entry) {
        entry = procfs_proc_create(pcb);
        if (entry) {
            procfs_proc_append(procfs, entry);
        }
        return;
    }

    for (size_t i = 0; i < entry->tcb_count; i++) {
        if (entry->tcbs[i]) {
            kfree(entry->tcbs[i]->tinfo);
            kfree(entry->tcbs[i]);
        }
    }
    kfree(entry->tcbs);

   	entry->tcb_count = pcb->thread_count;
    entry->tcbs = kcalloc(entry->tcb_count, sizeof(procfs_tcb_t *));

    for (size_t i = 0; i < pcb->thread_count; i++) {
    	if (pcb->threads[i]) {
        	entry->tcbs[i] = procfs_thread_create(pcb->threads[i]);
        }
    }

    kfree(entry->procinfo->data);
    kfree(entry->procinfo);
    entry->procinfo = procfs_procinfo_create(pcb);
}

void procfs_update_process(pcb_t *pcb) {
    if (!pcb) return;

    struct update_arg arg = { .pcb = pcb };

    procfs_foreach(procfs_update_process_foreach, &arg);
}

void procfs_add_process(pcb_t *pcb) {
    procfs_update_process(pcb);
}

void procfs_remove_process(pcb_t *pcb) {
    if (!pcb) return;

    procfs_foreach(procfs_remove_process_foreach, pcb);
}
