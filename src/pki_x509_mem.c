/* PKI_X509 object management */

#include <libpki/pki.h>

/* Static function for managing read callbacks for auto format downloading */

static void * __get_data_callback (PKI_MEM *io, const PKI_X509_CALLBACKS *cb,
				PKI_DATA_FORMAT format, PKI_CRED *cred);
/*
static PKI_MEM *__keypair_put_mem_value (EVP_PKEY *x, PKI_DATATYPE type,
                        PKI_MEM **pki_mem, PKI_DATA_FORMAT format,
                                PKI_CRED *cred, HSM *hsm);
*/

/* -------------------------- X509 mem Operations -------------------- */

/*! \brief Reads a PKI_X509 object from a PKI_MEM */

PKI_X509 *PKI_X509_get_mem ( PKI_MEM *mem, PKI_DATATYPE type, 
						PKI_CRED *cred, HSM *hsm ) {

	PKI_X509_STACK *sk = NULL;
	PKI_X509 *ret = NULL;

	if((sk = PKI_X509_STACK_get_mem ( mem, type, cred, hsm )) == NULL ) {
		return NULL;
	}

	ret = PKI_STACK_X509_pop ( sk );
	PKI_STACK_X509_free ( sk );

	return ret;
}

void * PKI_X509_get_mem_value ( PKI_MEM *mem, PKI_DATATYPE type,
						PKI_CRED *cred, HSM *hsm ) {
	PKI_X509 *tmp = NULL;
	void *ret = NULL;

	if(( tmp = PKI_X509_get_mem ( mem, type, cred, hsm )) == NULL ) {
		return NULL;
	}

	if ( tmp->value ) {
		ret = tmp->value;
		tmp->value = NULL;
	}

	PKI_X509_free ( tmp );

	return ret;
}


static void * __get_data_callback(PKI_MEM *mem, const PKI_X509_CALLBACKS *cb,
				PKI_DATA_FORMAT format, PKI_CRED *cred ) {

	PKI_IO *io = NULL;

	void *ret = NULL;
	char *pwd = NULL;

	// Checks the input
	if (!mem || !cb ) return NULL;

	// If we have credentials (password type), let's get a reference to it
	if ( cred && cred->password ) pwd = (char *) cred->password;

	// Let's now create the PKI_IO required for the function to process
	// the data correctly
	if ((io = BIO_new(BIO_s_mem())) == NULL)
	{
		// Error creating the IO channel, we have to abort
		PKI_ERROR(PKI_ERR_MEMORY_ALLOC, NULL);
		return NULL;
	}

	// We need to duplicate the buffer as the read functions might
	// alter the contents of the buffer
	PKI_MEM *dup_mem = PKI_MEM_dup(mem);
	if (dup_mem == NULL)
	{
		PKI_ERROR(PKI_ERR_MEMORY_ALLOC, NULL);

		PKI_Free(io);
		return NULL;
	}

	// Let's make the internals of the IO point to our PKI_MEM
	// data and size
	BUF_MEM *ptr = (BUF_MEM *) io->ptr;
	ptr->data = (char *) dup_mem->data;

#if ( OPENSSL_VERSION_NUMBER < 0x10000000L )
	ptr->length = (int) dup_mem->size;
#else
	ptr->length = (size_t) dup_mem->size;
#endif

	switch ( format )
	{
		case PKI_DATA_FORMAT_PEM :
			if( cb->read_pem ) {
				ret = cb->read_pem (io, NULL, NULL, pwd );
			}
			break;

		case PKI_DATA_FORMAT_ASN1 :
			if( cb->read_der ) {
				ret = cb->read_der ( io, NULL );
			}
			break;

		case PKI_DATA_FORMAT_TXT :
			if ( cb->read_txt ) {
				ret = cb->read_txt ( io, NULL );
			}
			break;

		case PKI_DATA_FORMAT_B64 :
			if (cb->read_b64)
			{
				ret = cb->read_b64(io, NULL);
			}
			else if (cb->read_der)
			{
				// PKI_MEM *rr = PKI_MEM_dup(mem);
				// if (rr == NULL) break;

				if (PKI_MEM_B64_decode(dup_mem, 76) == NULL &&
						PKI_MEM_B64_decode(dup_mem, 0) == NULL)
				{
					// Can not B64 decode
					break;
				}

				// Let's make the io internals point to the
				// B64 decoded PKI_MEM (rr)
				// ptr = (BUF_MEM *) io->ptr;
				// ptr->data = (char *) rr->data;

				// Now we "write" the data into the PKI_IO
				BIO_write(io, dup_mem->data, (int) dup_mem->size);

				// And use the DER reader to retrieve the
				// requested object
				ret = cb->read_der(io, NULL);

				/*
					rr = PKI_MEM_new_bio(io, NULL);
					PKI_MEM_B64_decode(rr, 76);
				(void) BIO_flush(io);
				// BIO_free_all(io);

				//if ((io = BIO_new(BIO_s_mem())) != NULL)
				// {
					BIO_write(io, rr->data, (int) rr->size);
				//	(void) BIO_flush(io);

					ret = cb->read_der(io, NULL);
					PKI_MEM_free(rr);
				//}
				 */
			}
			break;

		case PKI_DATA_FORMAT_XML :
			if ( cb->read_xml ) {
				ret = cb->read_xml ( io, NULL );
			}
			break;

		case PKI_DATA_FORMAT_URL :
			PKI_ERROR(PKI_ERR_NOT_IMPLEMENTED, NULL);
			break;

		default:
			break;
	}

	// We now have to "reset" the internal of the PKI_IO
	// object and free it
	if (io)
	{
		ptr = io->ptr;
		ptr->data = NULL;
		ptr->length = 0;

		BIO_free(io);
	}

	// Let's free the duplicated PKI_MEM structure
	if (dup_mem) PKI_Free(dup_mem);

	return ret;
}

/*! \brief Returns a stack of objects read from the passed PKI_MEM */

PKI_X509_STACK *PKI_X509_STACK_get_mem ( PKI_MEM *mem, 
			PKI_DATATYPE type, PKI_CRED *cred, HSM *hsm ) {

	PKI_X509_STACK *sk = NULL;
	PKI_X509 * x_obj = NULL;
	int i = 0;

	const PKI_X509_CALLBACKS *cb = NULL;

	// Checks for valid input
	if( !mem || mem->size <= 0 ) return NULL;

	if((cb = PKI_X509_CALLBACKS_get(type, hsm)) == NULL)
	{
		// We have not found the callbacks - we can not proceed
		// forward. Report the error and return NULL
		PKI_log_debug("Object type not supported [%d]", type);
		return NULL;
	}

	/* Check we have a good type */
	if((x_obj = PKI_X509_new (type, hsm)) == NULL)
	{
		// The object type is not supported, let's return NULL
		PKI_log_debug("Object type not supported [%d]", type);
		return NULL;
	}

	if((sk = PKI_STACK_X509_new()) == NULL )
	{
		// Memory allocation error
		PKI_ERROR(PKI_ERR_MEMORY_ALLOC, NULL);

		// Free reserved memory and return NULL
		PKI_X509_free(x_obj);

		// Nothing to return
		return NULL;
	}

	// We cycle through the different data types we support to enable
	// automatic data conversion on load.
	//
	// NOTE: we start from 1 as this is the first valid one after the
	//       unknown datatype (PKI_DATATYPE_UNKNOW)
	for (i = 1; i < PKI_DATA_FORMAT_SIZE; i++)
	{
		if ((x_obj->value = __get_data_callback(mem, cb, i, cred)) != NULL)
		{
			// Let's add the right properties to the object
			x_obj->cred = PKI_CRED_dup(cred);
			x_obj->type = type;
			x_obj->hsm  = hsm;
			x_obj->cb = cb;
			x_obj->ref = NULL;

			// Let's now save the object on the stack and return
			PKI_STACK_X509_push(sk, x_obj);

			// We managed to load the object, let's break the for loop
			return (sk);
		}
	}

	// If we reach here, no object was found - let's free the memory
	// and return null
	if (x_obj) PKI_X509_free(x_obj);

	// Let's return null
	return NULL;
}

/*! \brief Writes a PKI_X509 object to a PKI_MEM structure */

PKI_MEM *PKI_X509_put_mem (PKI_X509 *x, PKI_DATA_FORMAT format, 
				PKI_MEM **mem, PKI_CRED *cred ) {

	PKI_DATATYPE type = PKI_DATATYPE_UNKNOWN;

	// Checks the input
	if (!x || !x->value)
	{
		PKI_ERROR(PKI_ERR_PARAM_NULL, NULL);
		return ( NULL );
	}

	// Verifies we have the right set of callbacks attached to the
	// object we need to decode to memory
	if (!x->cb)
	{
		PKI_ERROR(PKI_ERR_CALLBACK_NULL, NULL);
		return NULL;
	}

	// Checks it is a supported datatype, if not - let's return null
	if ((type = PKI_X509_get_type ( x )) == PKI_DATATYPE_UNKNOWN)
	{
		PKI_ERROR(PKI_ERR_OBJECT_CREATE, NULL);
		return NULL;
	}

	// We need to be sure that the data structures are properly updated
	PKI_X509_set_modified ( x );

	// Returns the actual PKI_MEM with the encoded value
	return PKI_X509_put_mem_value ( x->value, type, mem, 
					format, cred, x->hsm );
}

/*! \brief Writes a PKI_X509_XXX_VALUE to a PKI_MEM structure */

PKI_MEM *PKI_X509_put_mem_value (void *x, PKI_DATATYPE type, 
			PKI_MEM **pki_mem, PKI_DATA_FORMAT format, 
				PKI_CRED *cred, HSM *hsm)
{
	PKI_IO *membio = NULL;
	const PKI_X509_CALLBACKS *cb = NULL;
	PKI_MEM *ret = NULL;
	int rv = 0;

	char *pwd = NULL;
	const EVP_CIPHER *enc = NULL;

	if ((membio = BIO_new(BIO_s_mem())) == NULL)
	{
		PKI_ERROR(PKI_ERR_OBJECT_CREATE, NULL);
		return NULL;
	}

	if ((cb = PKI_X509_CALLBACKS_get(type, hsm)) == NULL)
	{
		PKI_ERROR(PKI_ERR_CALLBACK_NULL, NULL);
		return NULL;
	}

	/* Check if we have to encrypt the key */
	if (cred && cred->password && strlen(cred->password) > 0)
	{
		pwd = (char *) cred->password;
		enc=EVP_aes_256_cbc();
	}
	else
	{
		enc=NULL;
	}

	switch (format)
	{
		case PKI_DATA_FORMAT_PEM:
			if (cb->to_pem_ex)
				rv = cb->to_pem_ex(membio, x, (void *) enc, NULL, 0, NULL, pwd );
			else if (cb->to_pem)
				rv = cb->to_pem ( membio, x );
			break;

		case PKI_DATA_FORMAT_URL:
		case PKI_DATA_FORMAT_ASN1:
			if (cb->to_der)
				rv = cb->to_der ( membio, x );
			else
				PKI_log_debug ( "NO ASN1 (type %d) callback ? %p",
					type, cb->to_der );
			break;

		case PKI_DATA_FORMAT_TXT:
			if (cb->to_txt) rv = cb->to_txt ( membio, x );
			break;

		case PKI_DATA_FORMAT_B64:
			if (cb->to_b64)
			{
				rv = cb->to_b64 ( membio, x );
			}
			else if (cb->to_der)
			{
				rv = cb->to_der(membio, x);
				if ((ret = PKI_MEM_new_bio(membio, pki_mem))
								!= NULL )
				{
					if (PKI_MEM_B64_encode(ret, 0) == NULL) rv = PKI_ERR;
					else rv = PKI_OK;
				}
				else
				{
					rv = 0;
				}
			}
			break;

		case PKI_DATA_FORMAT_XML:
			if (cb->to_xml)
				rv = cb->to_xml( membio, x );
			break;

		case PKI_DATA_FORMAT_UNKNOWN:
		default:
			PKI_log_debug("PKI_X509_put_mem_value()::Unsupported "
				"coding Format %d", format );
			rv = 0;
			break;
	}

	/* We already covered the case of B64 without a specific encoding callback */
	if ((format != PKI_DATA_FORMAT_B64) || (cb->to_b64 != NULL))
	{
		if (rv)
		{
			if ((ret = PKI_MEM_new_bio(membio, pki_mem)) == NULL )
			{
				PKI_log_err ("Can not convert BIO");
				if (membio) BIO_free_all (membio);
				return NULL;
			}
		}
		else
		{
			/* OpenSSL ERROR! */
			if (membio) BIO_free_all(membio);
			return ( NULL );
		}
	}

	BIO_free_all(membio);

	if (ret && (format == PKI_DATA_FORMAT_URL))
	{
		PKI_MEM *url_encoded = NULL;
	
		if (PKI_MEM_url_encode(ret, 1 ) != PKI_OK)
		{
			PKI_MEM_free(ret);
			return NULL;
		}
	}

	return ret;
}

/*! \brief Writes a stack of PKI_X509 to a PKI_MEM */

PKI_MEM * PKI_X509_STACK_put_mem ( PKI_X509_STACK *sk, PKI_DATA_FORMAT format, 
				PKI_MEM **pki_mem, PKI_CRED *cred, HSM *hsm ) {

	PKI_MEM *ret = NULL;
	int i = 0;

	if( !sk ) return (PKI_ERR);

	if ( pki_mem != NULL ) {
		if( *pki_mem == NULL ) {
			if((*pki_mem = PKI_MEM_new_null()) == NULL ) {
				PKI_ERROR(PKI_ERR_MEMORY_ALLOC, NULL);
				return NULL;
			}
			ret = *pki_mem;
		} else {
			ret = *pki_mem;
		}
	} else {
		ret = PKI_MEM_new_null();
	}

	if ( !ret ) return NULL;

	for( i = 0; i < PKI_STACK_X509_elements ( sk ); i++ ) {
		PKI_X509 *x_obj = NULL;
		PKI_MEM *tmp_mem = NULL;

		if((x_obj = PKI_STACK_X509_get_num( sk, i )) != NULL) {

			if((tmp_mem = PKI_X509_put_mem ( x_obj, 
					format, pki_mem, cred )) == NULL ) {
				PKI_log_debug("ERROR adding item %d to PKI_MEM", i);
				continue;
			}
			if ( x_obj->cred ) PKI_CRED_free ( x_obj->cred );
			if ( cred ) x_obj->cred = PKI_CRED_dup ( cred );
		}
	}

	return ret;
}

