/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ASSERTION:
 * A circular definition of structs (two structs defining each other as
 * members) should throw an error at compile time.
 *
 * SECTION: Structs and Unions/Structs
 *
 */


#pragma ident	"%Z%%M%	%I%	%E% SMI"
#pragma D option quiet


struct record {
   struct pirate p;
   int position;
   char content;
};


struct pirate {
   struct record r;
   int position;
   char content;
};

struct record rec;
struct pirate pir;

BEGIN
{
	rec.position = 0;
	rec.content = 'a';
	printf("rec.position: %d\nrec.content: %c\n",
	 rec.position, rec.content);

	exit(0);
}

