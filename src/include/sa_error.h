/*
 * Copyright 2008-2023 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

#pragma once

enum sa_error_code {
	SA_OK,
	SA_FAILED_BAD_REQUEST,
	SA_FAILED_BAD_CONFIG,
	SA_FAILED_INTERNAL,
	SA_FAILED_TIMEOUT
};

typedef struct sa_error_s
{
	enum sa_error_code code;
} sa_err;
