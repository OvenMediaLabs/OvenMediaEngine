# HLS Dump

The [LLHLS Dump feature](../../../../../streaming/low-latency-hls.md#dump) can be controlled with this API.

## Start Dump

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> /v1/vhosts/{vhost name}/apps/{app}/streams/{stream}:startHlsDump</summary>

#### Header

```http
Authorization: Basic {credentials}
Content-Type: application/json

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{
  "outputStreamName": "stream",
  "id": "unique_dump_id",
  "outputPath": "/tmp/",
  "playlist" : ["llhls.m3u8", "abr.m3u8"],
  "infoFile": "/home/abc/xxx/unique_info_file_name.info",
  "userData": "access_key_id='AKIAXFOT7EWH3ZA4XXXX'"
}

# outputStreamName (required)
  The name of the output stream created with OutputProfile.
# id (required)
  ID for this API request.
# outputPath (required)
  Directory path to output. The directory must be writable by the OME process. 
  OME will create the directory if it doesn't exist.
# playlist (optional)
  Dump the master playlist set in outputPath. It must be entered 
  in Json array format, and multiple playlists can be specified.
# infoFile (optional)
  This is the name of the DB file in which the information of the dumped files is 
  updated. If this value is not provided, no file is created. An error occurs 
  if a file with the same name exists. (More details below)
# userData (optional)
  If infoFile is specified, this data is written to infoFile. Does not work 
  if infoFile is not specified.
```

</details>

> ### Responses

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

The request has succeeded

#### **Header**

```
Content-Type: application/json
```

#### **Body**

```json
{
	"statusCode": 200,
	"message": "OK",
	"response": [
		"stream",
		"stream2"
	]
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Json array containing a list of stream names
```

</details>

<details>

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request. Body is not a Json Object or does not have a required value

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

Authentication required

#### **Header**

```http
WWW-Authenticate: Basic realm=”OvenMediaEngine”
```

#### **Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>

<details>

<summary><mark style="color:red;">404</mark> Not Found</summary>

The given vhost name or app name or stream name could not be found.

#### **Header**

```json
Content-Type: application/json
```

#### **Body**

```json
{
    "statusCode": 404,
    "message": "Could not find the application: [default/non-exists] (404)"
}
```

</details>

<details>

<summary><mark style="color:red;">500</mark> Internal Server Error</summary>

Unknown error

</details>

## Stop Dump

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> /v1/vhosts/&#x3C;vhost name>/apps/{app}/streams/{stream}:stopHlsDump</summary>

#### Header

```http
Authorization: Basic {credentials}
Content-Type: application/json

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{
  "outputStreamName": "stream",
  "id": "dump_id"
}

# outputStreamName (required)
 The name of the output stream created with OutputProfile.
# id (optional)
  This is the id passed when calling the startHlsDump API. 
  If id is not passed, all dump in progress at outputStreamName is aborted.
```

</details>

> ### Responses

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

The request has succeeded

#### **Header**

```
Content-Type: application/json
```

#### **Body**

```json
{
	"statusCode": 200,
	"message": "OK",
	"response": [
		"stream",
		"stream2"
	]
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Json array containing a list of stream names
```

</details>

<details>

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request. Body is not a Json Object or does not have a required value

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

Authentication required

#### **Header**

```http
WWW-Authenticate: Basic realm=”OvenMediaEngine”
```

#### **Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>

<details>

<summary><mark style="color:red;">404</mark> Not Found</summary>

The given vhost name or app name or stream name could not be found.

#### **Header**

```json
Content-Type: application/json
```

#### **Body**

```json
{
    "statusCode": 404,
    "message": "Could not find the application: [default/non-exists] (404)"
}
```

</details>

<details>

<summary><mark style="color:red;">500</mark> Internal Server Error</summary>

Unknown error

</details>

## InfoFile&#x20;

The info file is continuously updated after the dump file is written. It is in XML format and is as follows. will continue to be added.

```
<HLSDumpInfo>
  <UserData>~~~</UserData>
  <Stream>/default/app/stream</Stream>
  <Status>Running | Completed | Error </Status>
  <Items>
    <Item>
      <Seq>0</Seq>
      <Time>~~~</Time>
      <File>~~~</File>
    </Item>
    ...
    <Item>
      <Seq>1</Seq>
      <Time>~~~</Time>
      <File>/tmp/abc/xxx/298182/chunklist_0_video_llhls.m3u8</File>
    </Item>
    ...
    <Item>
      <Seq>2</Seq>
      <Time>~~~</Time>
      <File>chunklist_0_video_llhls.m3u8</File>
    </Item>
  </Items>
</HLSDumpInfo>
```
