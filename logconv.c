/* Reads several files, gzipped or not, and if it is in IIS format it detects
 * the fields by the #Fields: header and outputs in combined format
 * Copyright (c) 2009-2015 Fredrik Rambris <fredrik@rambris.com>. All rights reserved
 */

#include <stdio.h>
#define _GNU_SOURCE
#include <string.h>
#include <zlib.h>
#include <time.h>

void trim(char *str)
{
	int l;
	while((l=strlen(str))>0)
	{
		if(str[l-1]=='\n' || str[l-1]=='\r' || str[l-1]=='\t' || str[l-1]==' ') str[l-1]='\0';
		else break;
	}
}

/* Copied from http://code.nytimes.com/projects/dbslayer/browser/trunk/common/urldecode.c
   Slightly modified */
char * urldecode(char *in, char *rout)
{
	int i;
	char *out=rout;

	while(in != NULL && *in !='\0')
	{
		if(*in == '%' && *(in+1) !='\0' && *(in+2)!='\0' )
		{
			for(i=0, in++ ; i < 2; i++,in++) 
			{
				(*out) += ((isdigit(*in) ?  *in - '0' : ((tolower(*in) - 'a')) + 10) *  ( i ? 1 : 16));
			}
			out++;
		} 
		else if (*in == '+') 
		{
			*out++ = ' ';
			in++;
		}
		else 
		{
			*out++ = *in++;
		}
	}
	return  rout;
}

void convert_log_file(const char *path, const char *host)
{
	gzFile f;
	char buf[1024]; /* Read buffer */
	char d[100]; /* Date convert buffer */
	char u[1024]; /* URL parse buffer*/
	char *col; /* Column while tokenizing buffer */
	struct tm t; /* Time struct while converting */
	time_t tt; /* Used to read localtime to get timezone and DST */
	/* date time cs-method cs-uri-stem cs-uri-query cs-username c-ip cs-version cs(User-Agent) cs(Referer) sc-status sc-bytes cs-host */
	int pos[13]={-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; /* The positions of the above fields in the file */
	char *cols[100]; /* The separate columns after tokenization */
	int c; /* Column counter */
	int lcount=0, rcount=0; /* Rowcount */
	char *bname=strrchr(path, '/');
	if(bname==NULL) bname=(char *)path;
	else bname++;
	
	/* Read local time to be able to set correct UTC offset in output*/
	time(&tt);
	localtime_r(&tt, &t);
	tzset();

	if((f=gzopen(path, "rb")))
	{
		while(gzgets(f, buf, 1024))
		{
			rcount++;
			if(time(NULL)>tt)
			{
				time(&tt);
				fprintf(stderr, "     \r%s: %d rows read (%d rows per second)", bname, rcount, rcount-lcount);
				lcount=rcount;
			}
			/* Remove trailing whitespace and skip empty rows */
			trim(buf);
			if(buf[0]=='\0') continue;

			/* Parse fields header */
			if(!strncmp(buf,"#Fields:", 8))
			{
				col=strtok(buf," ");
				c=0;
				while((col=strtok(NULL, " ")))
				{
					c++;
					if(!strcmp(col, "date")) pos[0]=c-1;
					else if(!strcmp(col, "time")) pos[1]=c-1;
					else if(!strcmp(col, "cs-method")) pos[2]=c-1;
					else if(!strcmp(col, "cs-uri-stem")) pos[3]=c-1;
					else if(!strcmp(col, "cs-uri-query")) pos[4]=c-1;
					else if(!strcmp(col, "cs-username")) pos[5]=c-1;
					else if(!strcmp(col, "c-ip")) pos[6]=c-1;
					else if(!strcmp(col, "cs-version")) pos[7]=c-1;
					else if(!strcmp(col, "cs(User-Agent)")) pos[8]=c-1;
					else if(!strcmp(col, "cs(Referer)")) pos[9]=c-1;
					else if(!strcmp(col, "sc-status")) pos[10]=c-1;
					else if(!strcmp(col, "sc-bytes")) pos[11]=c-1;
					else if(!strcmp(col, "cs-host")) pos[12]=c-1;
				}
				for(c=0;c<13;c++)
				{
					if(pos[c]==-1)
					{
						if(c==12 && !host) continue;
						gzclose(f);
						fprintf(stderr, "Fields missing in '%s' (%d)\n", path, c);
						return;
					}
				}
			}
			else if(buf[0]!='#')
			{
				/* If we didn't find a Fields header just output the buffer */
				if(pos[0]==-1)
				{
					printf("%s\n", buf);
					continue;
				}

				/* Tokenize the row */
				c=0;
				cols[c++]=strtok(buf, " ");
				while((cols[c++]=strtok(NULL, " "))!=NULL)
				{
					if(c>99) break;
				}
				/* Make sure we get all fields */
				if(c<12) continue;

				/* If we have cs-host and we have specified -host and cs-host != host then skip the line */
				if(c>12 && host && strcmp(cols[pos[12]], host)) continue;

				/* Parse time and date and convert it to combined format */
				sprintf(d, "%s %s", cols[pos[0]], cols[pos[1]]);
				strptime(d, "%Y-%m-%d %T", &t);
				strftime(d, 40, "%d/%b/%Y:%T %z", &t);

				/* Client IP, nothing, date, Method, URL */
				printf("%s - %s [%s] \"%s %s", cols[pos[6]], cols[pos[5]], d, cols[pos[2]], cols[pos[3]]);
				/* QueryString if any */
				if(strcmp(cols[pos[4]], "-")) printf("?%s", cols[pos[4]]);
				/* Clear the URL decode buffer*/
				memset((void*)u, 0, 1024);
				/* HttpVersion, ErrorCode, ReturnBytes, Referer, UserAgent */
				printf(" %s\" %s %s \"%s\" \"%s\"\n", cols[pos[7]], cols[pos[10]], cols[pos[11]], cols[pos[9]], urldecode(cols[pos[8]], u));
			}
		}
		gzclose(f);
		fprintf(stderr, "\n");
	}
}

int main(int argc, char **argv)
{
	char *usage="Usage: %s [-host <host>] files...\n";
	int i=1;
	char *host=NULL;
	if(argc<2)
	{
		printf(usage, argv[0]);
		return 5;
	}
	if(!strcmp(argv[1], "-host"))
	{
		if(argc>3)
		{
			i=3;
			host=argv[2];
		}
		else
		{
			printf(usage, argv[0]);
			return 5;
		}
	}
	for(;i<argc;i++)
	{
		convert_log_file(argv[i], host);
	}
	return 0;
}
