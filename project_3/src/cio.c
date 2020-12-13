/** 
 * @file cio.c
 * @brief memory manager and operations for compressing JPEG IO.
 */

#include <string.h>
#include "cjpeg.h"
#include "cio.h"
#include "stdio.h"


/*
 * flush input and output of compress IO.
 */


bool
flush_cin_buffer(void *cio)
{
    mem_mgr *in = ((compress_io *) cio)->in;
    size_t len = in->end - in->set;
    // printf("len=%d\n", len);
    memset(in->set, 0, len);
    if (fread(in->set, sizeof(UINT8), len, in->fp) != len)
        return false;
	
	// reverse
#ifdef REVERSED
    fseek(in->fp, -len * 2, SEEK_CUR);
#endif
	
    in->pos = in->set;
    return true;
}

bool
flush_cout_buffer(void *cio)
{
    mem_mgr *out = ((compress_io *) cio)->out;
    size_t len = out->pos - out->set;
    if (fwrite(out->set, sizeof(UINT8), len, out->fp) != len)
        return false;
    memset(out->set, 0, len);
    out->pos = out->set;
    return true;
}


/*
 * init memory manager.
 */

void
init_mem(compress_io *cio, FILE *in_fp, int in_size, FILE *out_fp, int out_size)
{
    cio->in = (mem_mgr *) malloc(sizeof(mem_mgr));
    if (!cio->in)
        err_exit(BUFFER_ALLOC_ERR);
    cio->in->set = (UINT8 *) malloc(sizeof(UINT8) * in_size);
    if (!cio->in->set)
        err_exit(BUFFER_ALLOC_ERR);
    cio->in->pos = cio->in->set;
    cio->in->end = cio->in->set + in_size;
    cio->in->flush_buffer = flush_cin_buffer;
    cio->in->fp = in_fp;

    cio->out = (mem_mgr *) malloc(sizeof(mem_mgr));
    if (!cio->out)
        err_exit(BUFFER_ALLOC_ERR);
    cio->out->set = (UINT8 *) malloc(sizeof(UINT8) * out_size);
    if (!cio->out->set)
        err_exit(BUFFER_ALLOC_ERR);
    cio->out->pos = cio->out->set;
    cio->out->end = cio->out->set + out_size;
    cio->out->flush_buffer = flush_cout_buffer;
    cio->out->fp = out_fp;

    cio->temp_bits.len = 0;
    cio->temp_bits.val = 0;
}

void
free_mem(compress_io *cio)
{
    fflush(cio->out->fp);
    free(cio->in->set);
    free(cio->out->set);
    free(cio->in);
    free(cio->out);
}

/*
 * write operations.
 */

void
write_byte(compress_io *cio, UINT8 val)
{
    mem_mgr *out = cio->out;
    *(out->pos)++ = val & 0xFF;

    if (out->pos == out->end) {
        if (!(out->flush_buffer)(cio))
            err_exit(BUFFER_WRITE_ERR);
    }
}

void
write_word(compress_io *cio, UINT16 val)
{
    write_byte(cio, (val >> 8) & 0xFF);
    write_byte(cio, val & 0xFF);
}

void
write_marker(compress_io *cio, JPEG_MARKER mark)
{
    write_byte(cio, 0xFF);
    write_byte(cio, (int) mark);
}

void
write_bits(compress_io *cio, BITS bits)
{
    //Todo 写入压缩后的文件
    BITS *tmp = &(cio->temp_bits);
    UINT16 letter;
    UINT8 b1, b2;

    int len = bits.len + tmp->len;
	int over_flow = len - 16;
	//从左往右写
    if (over_flow >= 0) {
		//超出16bits，抹去低overflow位
        letter = tmp->val | bits.val >> over_flow;
		//先写高8位，b1是高8位
        b1 = letter >> 8;
        write_byte(cio, b1);
        if (b1 == 0xFF)
            write_byte(cio, 0);
		//再写低8位，b2是低8位
        b2 = letter & 0xFF;
        write_byte(cio, b2);
        if (b2 == 0xFF)
            write_byte(cio, 0);
		//tem->len更新为溢出的长度
        tmp->len = over_flow;
        tmp->val = bits.val << (16 - over_flow);
    }
    else {
		//不满16bits，放在高len位，低位为0
        tmp->len = len;
        tmp->val |= bits.val << -over_flow;
    }
}

void
write_align_bits(compress_io *cio)
{
	//Todo 对尾部进行补齐
    BITS *temp = &(cio->temp_bits);
    BITS align_bits;
    UINT8 byte;

	//尾部不满8bits的计算溢出的长度
	int over_flow = temp->len % 8;
    align_bits.len = 8 - over_flow;
	//低align->len位为1
    align_bits.val = (UINT16) ~0x0 >> over_flow;
	//写入补齐的bits
    write_bits(cio, align_bits);

	//如果补齐后不满16，只写高8位
    if (temp->len == 8) {	
        byte = temp->val >> 8;
        write_byte(cio, byte);
        if (byte == 0xFF)
            write_byte(cio, 0);
    }
}

